from pathlib import Path
from reportlab.pdfgen import canvas
from pypdf import PdfReader
import pypdfium2 as pdfium

root=Path('C:/Users/ximam/Documents/Universidad/EMBER-Hackathon')
out=root/'output/pdf/EMBER_Overnight_Update.pdf'
out.parent.mkdir(parents=True,exist_ok=True)
c=canvas.Canvas(str(out),pagesize=(960,540))
c.setTitle('EMBER — Overnight Progress Update')
for i in range(1,6):
    c.drawImage(str(root/f'tmp/ember-deck/pdf-slide-{i}.png'),0,0,width=960,height=540)
    c.showPage()
c.save()
r=PdfReader(out)
assert len(r.pages)==5
doc=pdfium.PdfDocument(str(out))
for i,page in enumerate(doc):
    assert page.get_size()==(960,540)
    page.render(scale=1).to_pil().save(str(root/f'tmp/ember-deck/pdf-check-{i+1}.png'))
print(f'Verified 5 widescreen pages: {out}')
