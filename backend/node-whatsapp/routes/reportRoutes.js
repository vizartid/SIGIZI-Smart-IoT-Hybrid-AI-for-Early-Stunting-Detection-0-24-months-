const express = require('express');

const router = express.Router();

const {
    analyzeChild
} = require('../services/geminiService');

const {
    generatePDF
} = require('../services/pdfService');

const {
    sendPDF
} = require('../services/whatsappService');

router.post('/generate-report', async (req, res) => {

    try {

        const data = req.body;

        /*
        ==========================
        GEMINI ANALYSIS
        ==========================
        */

        const analysis =
            await analyzeChild(data);

        /*
        ==========================
        GENERATE PDF
        ==========================
        */

        const pdfPath =
            await generatePDF(
                data,
                analysis
            );

        /*
        ==========================
        SEND WHATSAPP
        ==========================
        */

        await sendPDF(
            data.phone,
            pdfPath
        );

        res.json({
            success: true,
            analysis,
            pdfPath
        });

    } catch (error) {

        console.error(error);

        res.status(500).json({
            success: false,
            error: error.message
        });
    }
});

module.exports = router;