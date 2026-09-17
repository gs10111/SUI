#!/usr/bin/env python3
"""Converte um .docx em PDF sem LibreOffice, sem Word e sem pandoc.

POR QUE ISTO EXISTE. Esta maquina nao tem nenhum dos tres. A conversao acontece em Python puro:
o .docx e um ZIP de XML, entao le-se word/document.xml, monta-se HTML equivalente e o PyMuPDF
(fitz.Story + DocumentWriter) pagina esse HTML em A4, com cabecalho e rodape desenhados por
pagina.

O QUE ELE PRESERVA: hierarquia de titulos, paragrafos, listas, tabelas com bordas e as imagens
embutidas. O QUE ELE NAO PRESERVA: a diagramacao exata do Word - fonte por run, quebras manuais,
posicionamento flutuante. Serve para LER e CONFERIR o conteudo; o arquivo de entrega continua
sendo o .docx.

    python3 scripts/docx_para_pdf.py entrada.docx [saida.pdf]
"""
import html as _html
import io
import os
import re
import sys
import zipfile

import fitz

W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"

RE_P = re.compile(r"<w:p[ >].*?</w:p>|<w:p/>", re.S)
RE_T = re.compile(r"<w:t(?:\s+[^>/]*)?>(.*?)</w:t>", re.S)
RE_R = re.compile(r"<w:r[ >].*?</w:r>", re.S)
RE_ESTILO = re.compile(r'<w:pStyle w:val="([^"]+)"')
RE_TBL = re.compile(r"<w:tbl>.*?</w:tbl>", re.S)
RE_TR = re.compile(r"<w:tr[ >].*?</w:tr>", re.S)
RE_TC = re.compile(r"<w:tc>.*?</w:tc>", re.S)
RE_IMG = re.compile(r'r:embed="([^"]+)"')


def texto_de(fragmento):
    return _html.unescape("".join(RE_T.findall(fragmento)))


def corpo_do_paragrafo(p):
    """Texto do paragrafo com negrito e italico preservados por run."""
    partes = []
    for r in RE_R.findall(p):
        t = texto_de(r)
        if not t:
            continue
        t = _html.escape(t)
        if "<w:b/>" in r or "<w:b " in r:
            t = f"<b>{t}</b>"
        if "<w:i/>" in r or "<w:i " in r:
            t = f"<i>{t}</i>"
        partes.append(t)
    return "".join(partes) or _html.escape(texto_de(p))


def paragrafo_html(p, imagens, quebrar_depois=False):
    ids = RE_IMG.findall(p)
    if ids:
        saida = []
        for rid in ids:
            nome = imagens.get(rid)
            if nome:
                saida.append(f'<p class="fig"><img src="{nome}"></p>')
        if saida:
            return "".join(saida)

    texto = corpo_do_paragrafo(p).strip()
    if not texto:
        return ""

    estilo = RE_ESTILO.search(p)
    estilo = estilo.group(1) if estilo else ""

    # O sumario do Word e um campo com resultado em cache; sem o Word para recalcula-lo, os
    # numeros de pagina estariam errados. Sai do PDF de leitura em vez de sair errado.
    if estilo.startswith("TOC"):
        return ""
    if estilo == "Title":
        return f"<h1 class='capa'>{texto}</h1>"
    if estilo == "Subtitle":
        return f"<p class='sub'>{texto}</p>"
    if estilo == "Heading2":
        return f"<h2>{texto}</h2>"
    if estilo == "Heading3":
        return f"<h3>{texto}</h3>"
    if "<w:numPr>" in p or estilo == "ListParagraph":
        return f"<p class='item'>{texto}</p>"
    return f"<p>{texto}</p>"


def tabela_html(t):
    linhas = []
    for i, tr in enumerate(RE_TR.findall(t)):
        celulas = []
        for tc in RE_TC.findall(tr):
            conteudo = " ".join(
                c for c in (corpo_do_paragrafo(p).strip() for p in RE_P.findall(tc)) if c
            )
            tag = "th" if i == 0 else "td"
            celulas.append(f"<{tag}>{conteudo}</{tag}>")
        linhas.append("<tr>" + "".join(celulas) + "</tr>")
    return "<table>" + "".join(linhas) + "</table>"


CSS = """
body { font-family: serif; font-size: 10.5pt; line-height: 1.35; color: #111; }
h1.capa { font-size: 19pt; text-align: center; margin: 0 0 4pt 0; }
h1 { font-size: 16pt; margin: 14pt 0 6pt 0; }
p.sub { text-align: center; font-size: 12pt; margin: 0 0 14pt 0; }
h2 { font-size: 14pt; margin: 16pt 0 5pt 0; }
h3 { font-size: 11.5pt; margin: 12pt 0 4pt 0; }
p { margin: 0 0 6pt 0; text-align: justify; }
p.item { margin: 0 0 4pt 12pt; }
p.fig { text-align: center; margin: 10pt 0; }
img { width: 380px; }
table { width: 100%; border: 1px solid #666; margin: 8pt 0; }
th { background: #e8e8e8; font-weight: bold; font-size: 9.5pt; border: 1px solid #666; padding: 3px; }
td { font-size: 9.5pt; border: 1px solid #666; padding: 3px; }
"""


def docx_para_html(caminho):
    z = zipfile.ZipFile(caminho)
    doc = z.read("word/document.xml").decode("utf-8")
    rels = z.read("word/_rels/document.xml.rels").decode("utf-8")
    imagens = {
        rid: os.path.basename(alvo)
        for rid, alvo in re.findall(r'Id="([^"]+)"[^>]*Target="(media/[^"]+)"', rels)
    }

    corpo = doc[doc.index("<w:body>"):]
    legendas = [m.start() for m in re.finditer(r"<w:p[ >].*?</w:p>", corpo, re.S)
                if 'w:val="Subtitle"' in m.group(0)]
    fim_da_capa = legendas[-1] if legendas else -1
    # Percorre body na ORDEM: tabela e paragrafo intercalam, e processar por tipo embaralharia
    # o documento.
    pecas, pos = [], 0
    for m in re.finditer(r"<w:tbl>.*?</w:tbl>|<w:p[ >].*?</w:p>|<w:p/>", corpo, re.S):
        bloco = m.group(0)
        if bloco.startswith("<w:tbl>"):
            pecas.append(tabela_html(bloco))
        else:
            if "<w:instrText" in bloco:
                continue
            html_p = paragrafo_html(bloco, imagens)
            # Sem o sumario (que sai deste PDF), a capa e a primeira pagina de conteudo
            # colapsariam numa so, com o titulo aparecendo duas vezes. A quebra vai depois da
            # ULTIMA legenda de capa, e nao de cada uma.
            if m.start() == fim_da_capa and html_p:
                html_p += "<div style='page-break-after: always'></div>"
            pecas.append(html_p)
        pos = m.end()

    arquivo_imgs = {os.path.basename(a): z.read("word/" + a)
                    for a in [t for _, t in re.findall(r'Id="([^"]+)"[^>]*Target="(media/[^"]+)"', rels)]}
    return f"<html><head><style>{CSS}</style></head><body>" + "".join(pecas) + "</body></html>", arquivo_imgs


def converter(entrada, saida, titulo_cabecalho=None):
    corpo, imagens = docx_para_html(entrada)

    memoria = fitz.Archive()
    for nome, dados in imagens.items():
        memoria.add(dados, nome)

    A4 = fitz.paper_rect("a4")
    margem = 56  # ~2 cm
    area = A4 + (margem, margem + 26, -margem, -(margem + 18))

    historia = fitz.Story(html=corpo, archive=memoria)
    escritor = fitz.DocumentWriter(saida)
    paginas = 0
    while True:
        dispositivo = escritor.begin_page(A4)
        mais, _ = historia.place(area)
        historia.draw(dispositivo)
        escritor.end_page()
        paginas += 1
        if not mais:
            break
    escritor.close()

    # Cabecalho e rodape sao desenhados DEPOIS: o Story pagina o corpo e so entao se sabe
    # quantas paginas ha.
    d = fitz.open(saida)
    cab = titulo_cabecalho or os.path.basename(entrada)
    for i, pagina in enumerate(d):
        pagina.insert_text((margem, margem - 6), cab, fontsize=8, color=(0.35, 0.35, 0.35))
        pagina.draw_line(fitz.Point(margem, margem + 2),
                         fitz.Point(A4.width - margem, margem + 2), color=(0.75, 0.75, 0.75))
        pagina.draw_line(fitz.Point(margem, A4.height - margem - 12),
                         fitz.Point(A4.width - margem, A4.height - margem - 12),
                         color=(0.75, 0.75, 0.75))
        pagina.insert_text((margem, A4.height - margem), "www.dieletrons.com",
                           fontsize=8, color=(0.35, 0.35, 0.35))
        rodape = f"{i + 1} / {paginas}"
        pagina.insert_text((A4.width - margem - fitz.get_text_length(rodape, fontsize=8),
                            A4.height - margem), rodape, fontsize=8, color=(0.35, 0.35, 0.35))
    d.saveIncr()
    d.close()
    return paginas


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    entrada = sys.argv[1]
    saida = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(entrada)[0] + ".pdf"
    n = converter(entrada, saida,
                  "Manual do Cliente — Supervisor de Inclinação SUI-DI141388XY")
    print(f"{saida}: {n} paginas")
