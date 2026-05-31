process.on('uncaughtException', (err) => {

    console.log('ERROR:', err.message);
});

process.on('unhandledRejection', (reason) => {

    console.log('REJECTION:', reason);
});
const {
    analyzeChild
} = require('./services/geminiService');

const {
    generatePDF
} = require('./services/pdfService');

const {
    MessageMedia
} = require('whatsapp-web.js');

const express = require('express');

require('dotenv').config();

const {
    client
} = require('./services/whatsappService');

const app = express();

/*
=====================================
ROOT
=====================================
*/

app.get('/', (req, res) => {

    res.send('SIGIZI SERVER RUNNING');
});

/*
=====================================
TEST WHATSAPP
=====================================
*/

app.get('/test-wa', async (req, res) => {

    try {

        /*
        DATA ANAK
        */

        const data = {

            nama: 'Budi',

            usia: 12,

            gender: 'Laki-laki',

            tinggi: 70,

            berat: 7.8
        };

        /*
        NOMOR TUJUAN
        */

        const number =
            '6283143632591@c.us';

        /*
        ==========================
        GEMINI ANALYSIS
        ==========================
        */

        console.log('AI ANALYSIS...');

        const analysis =
            await analyzeChild(data);

        console.log(analysis);

        /*
        ==========================
        GENERATE PDF
        ==========================
        */

        console.log('GENERATE PDF...');

        const pdfPath =
            await generatePDF(
                data,
                analysis
            );

        /*
        ==========================
        SEND PDF
        ==========================
        */

        console.log('SEND PDF...');

        const media =
            MessageMedia.fromFilePath(
                pdfPath
            );

        await client.sendMessage(
            number,
            media,
            {
                caption:
                    'Laporan SIGIZI'
            }
        );

        res.send(
            'PDF berhasil dikirim'
        );

    } catch (error) {

        console.log(error);

        res.send(error.message);
    }
});

app.listen(3000, () => {

    console.log(
        'Server running on port 3000'
    );
});