const {
    GoogleGenerativeAI
} = require('@google/generative-ai');

require('dotenv').config();

const genAI =
    new GoogleGenerativeAI(
        process.env.GEMINI_API_KEY
    );

/*
=====================================
ANALYZE CHILD
=====================================
*/

async function analyzeChild(data) {

    const model =
        genAI.getGenerativeModel({
            model: 'gemini-1.5-flash'
        });

    const prompt = `

Anda adalah AI kesehatan balita.

Analisis data berikut:

Nama: ${data.nama}
Usia: ${data.usia} bulan
Jenis Kelamin: ${data.gender}
Tinggi Badan: ${data.tinggi} cm
Berat Badan: ${data.berat} kg

Berikan:
1. Status pertumbuhan
2. Risiko stunting
3. Rekomendasi nutrisi
4. Saran monitoring

Gunakan bahasa sederhana untuk orang tua.
`;

    let attempts = 0;

    while (attempts < 3) {

        try {

            const result =
                await model.generateContent(
                    prompt
                );

            return result.response.text();

        } catch (error) {

            attempts++;

            console.log(
                `Retry AI ${attempts}`
            );

            await new Promise(resolve =>
                setTimeout(resolve, 3000)
            );
        }
    }

    return `
Analisis sementara tidak tersedia.
`;
}

/*
=====================================
EXPORT
=====================================
*/

module.exports = {
    analyzeChild
};