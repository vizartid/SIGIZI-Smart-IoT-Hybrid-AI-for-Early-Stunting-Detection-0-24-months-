import asyncio
import json
import os
from typing import Dict, Any
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.middleware.cors import CORSMiddleware
import firebase_admin
from firebase_admin import credentials, firestore
import aiohttp
from datetime import datetime
import logging

# Setup logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# ==================== Firebase Initialization ====================
# Use environment variable for service account path or default
firebase_creds_path = os.environ.get("FIREBASE_CREDENTIALS", "firebase_credentials.json")
if not os.path.exists(firebase_creds_path):
    # For development, you can use anonymous or mock. But for production, set credentials.
    logger.warning(f"Firebase credentials not found at {firebase_creds_path}. Using emulator or mock.")
    # Optionally initialize without credentials (for Firestore emulator)
    firebase_admin.initialize_app()
else:
    cred = credentials.Certificate(firebase_creds_path)
    firebase_admin.initialize_app(cred)

db = firestore.client()

# ==================== FastAPI App ====================
app = FastAPI(title="SIGIZI Backend", description="WebSocket server for ESP32 data with ML inference")

# Allow CORS for dashboard (if needed)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ==================== ML Service Configuration ====================
ML_SERVICE_URL = os.environ.get("ML_SERVICE_URL", "http://localhost:5001/predict")
# Timeout for ML request (seconds)
ML_TIMEOUT = 5.0

# ==================== WebSocket Connection Manager ====================
class ConnectionManager:
    def __init__(self):
        self.active_connections: Dict[str, WebSocket] = {}  # device_id -> websocket

    async def connect(self, websocket: WebSocket, device_id: str):
        await websocket.accept()
        self.active_connections[device_id] = websocket
        logger.info(f"Device {device_id} connected")

    def disconnect(self, device_id: str):
        if device_id in self.active_connections:
            del self.active_connections[device_id]
            logger.info(f"Device {device_id} disconnected")

    async def send_message(self, message: dict, device_id: str):
        if device_id in self.active_connections:
            try:
                await self.active_connections[device_id].send_json(message)
            except Exception as e:
                logger.error(f"Error sending to {device_id}: {e}")

manager = ConnectionManager()

# ==================== Helper: Call ML Service ====================
async def call_ml_service(umur_bulan: int, gender: str, berat_kg: float, tinggi_cm: float) -> Dict[str, Any]:
    """Call Flask ML service to get Z-scores and classification."""
    payload = {
        "umur_bulan": umur_bulan,
        "jenis_kelamin": gender,   # "L" or "P"
        "berat_kg": berat_kg,
        "tinggi_cm": tinggi_cm
    }
    async with aiohttp.ClientSession() as session:
        try:
            async with session.post(ML_SERVICE_URL, json=payload, timeout=ML_TIMEOUT) as resp:
                if resp.status == 200:
                    result = await resp.json()
                    logger.info(f"ML response: {result}")
                    return result
                else:
                    logger.error(f"ML service error: {resp.status} - {await resp.text()}")
                    return None
        except asyncio.TimeoutError:
            logger.error("ML service timeout")
            return None
        except Exception as e:
            logger.error(f"ML service exception: {e}")
            return None

# ==================== Store to Firestore ====================
async def store_measurement(device_id: str, measurement: dict, ml_result: dict = None):
    """Store measurement data to Firestore under toddler's subcollection."""
    try:
        # Assume device_id is the toddler's RFID UID (or use separate mapping)
        toddler_ref = db.collection('toddlers').document(device_id)
        # Ensure toddler document exists (could have name, etc.)
        toddler_ref.set({"last_seen": firestore.SERVER_TIMESTAMP}, merge=True)
        
        # Add measurement to subcollection 'measurements'
        measurement_data = {
            "timestamp": datetime.utcnow(),
            "weight_kg": measurement.get("weight_kg"),
            "length_cm": measurement.get("length_cm"),
            "umur_bulan": measurement.get("umur_bulan", None),  # might need to retrieve from DB or pass
        }
        if ml_result:
            measurement_data["zscore_bb"] = ml_result.get("zscore_bb")
            measurement_data["zscore_tb"] = ml_result.get("zscore_tb")
            measurement_data["status_gizi"] = ml_result.get("status_gizi")
            measurement_data["status_stunting"] = ml_result.get("status_stunting")
        
        # Add to measurements collection
        measurements_ref = toddler_ref.collection('measurements')
        measurements_ref.add(measurement_data)
        logger.info(f"Stored measurement for {device_id}")
        
        # Also update a real-time trigger node for WhatsApp notifications
        if ml_result and ml_result.get("status_stunting") != "Normal":
            # Write to Realtime Database or Firestore trigger collection
            trigger_ref = db.collection('alerts').document()
            trigger_ref.set({
                "toddler_id": device_id,
                "status_stunting": ml_result.get("status_stunting"),
                "zscore_tb": ml_result.get("zscore_tb"),
                "timestamp": firestore.SERVER_TIMESTAMP,
                "notified": False
            })
            logger.info(f"Alert created for {device_id}")
            
    except Exception as e:
        logger.error(f"Firestore error: {e}")

# ==================== WebSocket Endpoint ====================
@app.websocket("/ws/{device_id}")
async def websocket_endpoint(websocket: WebSocket, device_id: str):
    await manager.connect(websocket, device_id)
    try:
        while True:
            # Receive JSON message from ESP32
            data = await websocket.receive_json()
            logger.info(f"Received from {device_id}: {data}")
            
            # Expected fields: id (optional, but device_id used), weight_kg, length_cm, umur_bulan (optional)
            # If umur_bulan not provided, fetch from previous measurement or default.
            weight = data.get("weight_kg")
            length = data.get("length_cm")
            umur_bulan = data.get("umur_bulan")
            
            if weight is None or length is None:
                await manager.send_message({"error": "Missing weight_kg or length_cm"}, device_id)
                continue
            
            # If umur_bulan not provided, try to retrieve from last measurement
            if umur_bulan is None:
                # Query last measurement for this toddler
                toddler_ref = db.collection('toddlers').document(device_id)
                measurements = toddler_ref.collection('measurements').order_by('timestamp', direction=firestore.Query.DESCENDING).limit(1).get()
                if len(list(measurements)) > 0:
                    # Get umur_bulan from the toddler document? Better to store in toddler profile.
                    # For now, assume we have a 'profile' subcollection or field.
                    # Simpler: read from toddler doc field 'umur_bulan' if exists.
                    toddler_doc = toddler_ref.get()
                    if toddler_doc.exists and 'umur_bulan' in toddler_doc.to_dict():
                        umur_bulan = toddler_doc.to_dict()['umur_bulan']
                    else:
                        # Fallback: ask user to input via dashboard
                        await manager.send_message({"error": "Umur bulan tidak diketahui, silakan update profil balita"}, device_id)
                        continue
                else:
                    await manager.send_message({"error": "Umur bulan tidak ditemukan, silakan input melalui dashboard"}, device_id)
                    continue
            
            # Get gender from toddler profile
            toddler_doc = db.collection('toddlers').document(device_id).get()
            if toddler_doc.exists and 'jenis_kelamin' in toddler_doc.to_dict():
                gender = toddler_doc.to_dict()['jenis_kelamin']
            else:
                gender = "L"  # default male, but should be set via dashboard
            
            # Call ML service
            ml_result = await call_ml_service(umur_bulan, gender, weight, length)
            
            # Store to Firestore
            await store_measurement(device_id, {"weight_kg": weight, "length_cm": length, "umur_bulan": umur_bulan}, ml_result)
            
            # Send response back to ESP32
            if ml_result:
                response = {
                    "status": "ok",
                    "zscore_tb": ml_result.get("zscore_tb"),
                    "status_stunting": ml_result.get("status_stunting"),
                    "recommendation": ml_result.get("recommendation", "")  # optional
                }
            else:
                response = {"status": "error", "message": "ML inference failed"}
            await manager.send_message(response, device_id)
            
    except WebSocketDisconnect:
        manager.disconnect(device_id)
    except Exception as e:
        logger.error(f"WebSocket error: {e}")
        manager.disconnect(device_id)

# ==================== Health Check ====================
@app.get("/health")
async def health_check():
    return {"status": "healthy"}

# ==================== HTTP Endpoint for manual data entry (optional) ====================
@app.post("/measurement")
async def manual_measurement(data: dict):
    # Similar logic but without WebSocket, for testing
    device_id = data.get("id")
    if not device_id:
        raise HTTPException(status_code=400, detail="Missing id")
    # Reuse functions
    # ... (similar to above)
    return {"status": "ok"}