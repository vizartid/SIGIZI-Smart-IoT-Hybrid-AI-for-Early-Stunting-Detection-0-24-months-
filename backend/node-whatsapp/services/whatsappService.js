console.log('WHATSAPP SERVICE LOADED');

const {
    Client,
    LocalAuth
} = require('whatsapp-web.js');

const qrcode =
    require('qrcode-terminal');

/*
=====================================
CLIENT
=====================================
*/

const client = new Client({

    authStrategy:
        new LocalAuth(),

    puppeteer: {

        headless: false,


        args: [

            '--no-sandbox',

            '--disable-setuid-sandbox',

            '--disable-dev-shm-usage',

            '--disable-accelerated-2d-canvas',

            '--disable-gpu'
        ]
    },

    webVersionCache: {
        type: 'none'
    }
});

/*
=====================================
QR
=====================================
*/

client.on('qr', (qr) => {

    console.log('\nSCAN QR WHATSAPP\n');

    qrcode.generate(qr, {
        small: true
    });
});

/*
=====================================
READY
=====================================
*/

client.on('ready', () => {

    console.log('WhatsApp Ready!');
});

/*
=====================================
AUTH
=====================================
*/

client.on('authenticated', () => {

    console.log('Authenticated!');
});

/*
=====================================
FAIL
=====================================
*/

client.on('auth_failure', (msg) => {

    console.log('Auth Failure:', msg);
});

/*
=====================================
DISCONNECT
=====================================
*/

client.on('disconnected', (reason) => {

    console.log('Disconnected:', reason);
});

/*
=====================================
INIT
=====================================
*/

console.log('Initializing WhatsApp...');

client.initialize();

/*
=====================================
EXPORT
=====================================
*/

module.exports = {
    client
};