# websocket_handler.py
import json
import logging
from typing import Dict
from fastapi import WebSocket, WebSocketDisconnect
from .ml_client import call_ml_service
from .firestore_client import store_measurement, get_toddler_profile

logger = logging.getLogger(__name__)

class WebSocketHandler:
    def __init__(self):
        self.active_connections: Dict[str, WebSocket] = {}

    async def connect(self, websocket: WebSocket, device_id: str):
        await websocket.accept()
        self.active_connections[device_id] = websocket
        logger.info(f"Device {device_id} connected")

    def disconnect(self, device_id: str):
        if device_id in self.active_connections:
            del self.active_connections[device_id]

    async def send_message(self, message: dict, device_id: str):
        if device_id in self.active_connections:
            await self.active_connections[device_id].send_json(message)

    async def handle_message(self, websocket: WebSocket, device_id: str, data: dict):
        # Process incoming message
        weight = data.get("weight_kg")
        length = data.get("length_cm")
        if weight is None or length is None:
            await self.send_message({"error": "Missing weight_kg or length_cm"}, device_id)
            return
        
        # Get toddler profile from Firestore
        profile = await get_toddler_profile(device_id)
        if not profile:
            await self.send_message({"error": "Profile not found"}, device_id)
            return
        
        umur_bulan = profile.get("umur_bulan")
        gender = profile.get("jenis_kelamin")
        if umur_bulan is None:
            await self.send_message({"error": "Umur not set"}, device_id)
            return
        
        ml_result = await call_ml_service(umur_bulan, gender, weight, length)
        await store_measurement(device_id, {"weight_kg": weight, "length_cm": length, "umur_bulan": umur_bulan}, ml_result)
        
        response = {"status": "ok", "zscore_tb": ml_result.get("zscore_tb"), "status_stunting": ml_result.get("status_stunting")}
        await self.send_message(response, device_id)

handler = WebSocketHandler()

async def websocket_endpoint(websocket: WebSocket, device_id: str):
    await handler.connect(websocket, device_id)
    try:
        while True:
            data = await websocket.receive_json()
            await handler.handle_message(websocket, device_id, data)
    except WebSocketDisconnect:
        handler.disconnect(device_id)