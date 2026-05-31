const PDFDocument =
    require('pdfkit');

const fs =
    require('fs');

const path =
    require('path');

async function generatePDF(
    data,
    analysis
) {

    return new Promise((resolve) => {

        const filePath =
            path.join(
                __dirname,
                `../report-${Date.now()}.pdf`
            );

        const doc =
            new PDFDocument();

        doc.pipe(
            fs.createWriteStream(filePath)
        );

        doc.fontSize(20)
            .text('SIGIZI REPORT');

        doc.moveDown();

        doc.text(`Nama: ${data.nama}`);
        doc.text(`Usia: ${data.usia}`);
        doc.text(`Tinggi: ${data.tinggi}`);
        doc.text(`Berat: ${data.berat}`);

        doc.moveDown();

        doc.text(analysis);

        doc.end();

        doc.on('finish', () => {

            resolve(filePath);
        });
    });
}

module.exports = {
    generatePDF
};