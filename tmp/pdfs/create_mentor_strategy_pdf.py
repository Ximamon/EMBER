from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (
    BaseDocTemplate,
    Frame,
    KeepTogether,
    PageBreak,
    PageTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
)


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "output" / "pdf" / "EMBER-estrategias-reunion-mentores.pdf"

NAVY = colors.HexColor("#12233F")
BLUE = colors.HexColor("#2E6F95")
EMBER = colors.HexColor("#E8752B")
PALE_BLUE = colors.HexColor("#EAF3F8")
PALE_ORANGE = colors.HexColor("#FFF1E8")
PALE_GREY = colors.HexColor("#F4F6F8")
MID_GREY = colors.HexColor("#687586")
TEXT = colors.HexColor("#233142")
WHITE = colors.white


styles = getSampleStyleSheet()
styles.add(ParagraphStyle(
    name="CoverTitle", parent=styles["Title"], fontName="Helvetica-Bold",
    fontSize=30, leading=35, textColor=NAVY, alignment=TA_LEFT, spaceAfter=7 * mm,
))
styles.add(ParagraphStyle(
    name="CoverSubtitle", parent=styles["Normal"], fontName="Helvetica",
    fontSize=14, leading=19, textColor=BLUE, spaceAfter=13 * mm,
))
styles.add(ParagraphStyle(
    name="H1Custom", parent=styles["Heading1"], fontName="Helvetica-Bold",
    fontSize=20, leading=24, textColor=NAVY, spaceBefore=2 * mm, spaceAfter=5 * mm,
))
styles.add(ParagraphStyle(
    name="H2Custom", parent=styles["Heading2"], fontName="Helvetica-Bold",
    fontSize=12, leading=15, textColor=BLUE, spaceBefore=4 * mm, spaceAfter=2 * mm,
))
styles.add(ParagraphStyle(
    name="BodyCustom", parent=styles["BodyText"], fontName="Helvetica",
    fontSize=9.5, leading=13.5, textColor=TEXT, spaceAfter=2.5 * mm,
))
styles.add(ParagraphStyle(
    name="SmallCustom", parent=styles["BodyText"], fontName="Helvetica",
    fontSize=8, leading=10.5, textColor=MID_GREY, spaceAfter=1.5 * mm,
))
styles.add(ParagraphStyle(
    name="BoxTitle", parent=styles["BodyText"], fontName="Helvetica-Bold",
    fontSize=10.5, leading=13, textColor=NAVY, spaceAfter=1.5 * mm,
))
styles.add(ParagraphStyle(
    name="BoxBody", parent=styles["BodyText"], fontName="Helvetica",
    fontSize=9, leading=12.5, textColor=TEXT,
))
styles.add(ParagraphStyle(
    name="CodeCustom", parent=styles["Code"], fontName="Courier",
    fontSize=8.2, leading=11, textColor=NAVY, leftIndent=4 * mm,
    rightIndent=4 * mm, spaceBefore=2 * mm, spaceAfter=3 * mm,
))
styles.add(ParagraphStyle(
    name="TableHead", parent=styles["BodyText"], fontName="Helvetica-Bold",
    fontSize=8.5, leading=10.5, textColor=WHITE,
))
styles.add(ParagraphStyle(
    name="TableBody", parent=styles["BodyText"], fontName="Helvetica",
    fontSize=8.2, leading=10.5, textColor=TEXT,
))
styles.add(ParagraphStyle(
    name="TableBodyBold", parent=styles["BodyText"], fontName="Helvetica-Bold",
    fontSize=8.2, leading=10.5, textColor=NAVY,
))


def P(text, style="BodyCustom"):
    return Paragraph(text, styles[style])


def bullet(text):
    return P(f"- {text}", "BodyCustom")


def info_box(title, body, background=PALE_BLUE, border=BLUE):
    table = Table([[P(title, "BoxTitle")], [P(body, "BoxBody")]], colWidths=[174 * mm])
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), background),
        ("BOX", (0, 0), (-1, -1), 0.8, border),
        ("LEFTPADDING", (0, 0), (-1, -1), 5 * mm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 5 * mm),
        ("TOPPADDING", (0, 0), (-1, -1), 3 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3 * mm),
    ]))
    return table


def make_table(rows, widths, header=True):
    converted = []
    for row_index, row in enumerate(rows):
        converted.append([
            P(str(cell), "TableHead" if header and row_index == 0 else "TableBody")
            for cell in row
        ])
    table = Table(converted, colWidths=widths, repeatRows=1 if header else 0, hAlign="LEFT")
    commands = [
        ("GRID", (0, 0), (-1, -1), 0.35, colors.HexColor("#C9D3DD")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 2.5 * mm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 2.5 * mm),
        ("TOPPADDING", (0, 0), (-1, -1), 2.2 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 2.2 * mm),
    ]
    if header:
        commands.extend([
            ("BACKGROUND", (0, 0), (-1, 0), NAVY),
            ("TEXTCOLOR", (0, 0), (-1, 0), WHITE),
        ])
        for row_index in range(1, len(rows)):
            if row_index % 2 == 0:
                commands.append(("BACKGROUND", (0, row_index), (-1, row_index), PALE_GREY))
    table.setStyle(TableStyle(commands))
    return table


def footer(canvas, document):
    canvas.saveState()
    width, height = A4
    canvas.setStrokeColor(colors.HexColor("#D9E1E8"))
    canvas.setLineWidth(0.5)
    canvas.line(18 * mm, 14 * mm, width - 18 * mm, 14 * mm)
    canvas.setFont("Helvetica", 7.5)
    canvas.setFillColor(MID_GREY)
    canvas.drawString(18 * mm, 9 * mm, "EMBER | Preparacion de reunion con mentores")
    canvas.drawRightString(width - 18 * mm, 9 * mm, f"Pagina {document.page}")
    canvas.restoreState()


def build_story():
    story = []
    story.append(Spacer(1, 22 * mm))
    story.append(P("EMBER", "CoverTitle"))
    story.append(P("Estrategias para la reunion con los mentores del hackathon", "CoverSubtitle"))
    story.append(info_box(
        "Situacion de partida",
        "El proyecto ya dispone de un baseline CPU funcional, reproducible y medible. La reunion debe servir para elegir una unica primera aceleracion y acordar como demostrarla con rigor.",
        PALE_ORANGE, EMBER,
    ))
    story.append(Spacer(1, 10 * mm))
    story.append(P("Mensaje principal", "H2Custom"))
    story.append(P(
        "Tenemos un simulador C++17 con SoA, doble bufer, vecindario de Moore, RNG determinista por hash, CLI, exportacion y tests. El siguiente paso no es anadir funcionalidades al azar: es conservar esta implementacion como oracle de correccion y medir una aceleracion comparable.",
        "BodyCustom",
    ))
    story.append(P("Objetivo concreto de la reunion", "H2Custom"))
    for item in [
        "Fijar la metrica principal: speedup respecto al baseline, throughput, escalabilidad o eficiencia.",
        "Elegir la primera ruta: OpenMP por escenarios, AVX2 dentro del kernel o CUDA.",
        "Acordar hardware, profiler y protocolo de benchmark disponibles en MareNostrum.",
        "Definir que significa reproducibilidad entre el backend CPU y una futura GPU.",
    ]:
        story.append(bullet(item))
    story.append(Spacer(1, 5 * mm))
    story.append(P("Fecha de preparacion: 15 de septiembre de 2026", "SmallCustom"))
    story.append(PageBreak())

    story.append(P("1. Estado actual que conviene presentar", "H1Custom"))
    status_rows = [
        ("Area", "Hecho", "Lectura para la reunion"),
        ("Nucleo", "C++17, SoA, doble bufer, Moore de 8 vecinos, viento, humedad, vegetacion, pendiente y combustible.", "La estructura es local por celda y portable a GPU."),
        ("Reproducibilidad", "Hash de 64 bits derivado de semilla, escenario, paso y celda.", "No depende del orden de recorrido; buena base para OpenMP/CUDA."),
        ("Validacion", "CTest pasa; ember_tests contiene 10 grupos de casos.", "La referencia de correccion existe antes de optimizar."),
        ("Interfaz", "CLI, estadisticas, CSV y PPM.", "Permite demo y benchmark sin GUI."),
        ("Documentacion", "README, modelo simplificado, notas CUDA y Doxygen.", "La historia tecnica se puede explicar a terceros."),
    ]
    story.append(make_table(status_rows, [29 * mm, 76 * mm, 69 * mm]))
    story.append(Spacer(1, 5 * mm))
    story.append(info_box(
        "Evidencia que ya podeis ensenar",
        "La validacion actual pasa en el build principal y en los builds experimentales avx2/scalar. El benchmark CSV contiene 80 filas: 40 de Windows y 40 de un entorno Linux. El throughput combinado varia aproximadamente entre 17 y 69 millones de cell updates/s, por lo que esos datos deben normalizarse antes de usarlos para afirmar un speedup.",
        PALE_BLUE, BLUE,
    ))
    story.append(Spacer(1, 4 * mm))
    story.append(P("Rama experimental", "H2Custom"))
    story.append(P(
        "Existe una rama Git llamada avx (commit 3131237) con una ruta AVX2 opcional, deteccion de soporte en runtime y fallback escalar. La rama main sigue siendo la referencia. Presentad AVX2 como experimento reproducible pendiente de integracion, no como una funcionalidad ya consolidada en main.",
        "BodyCustom",
    ))
    story.append(PageBreak())

    story.append(P("2. Preguntas de alto valor para los mentores", "H1Custom"))
    question_rows = [
        ("Tema", "Pregunta exacta", "Postura recomendada"),
        ("Criterio de exito", "Que valorara mas el jurado: speedup, throughput, escalabilidad, energia o modelo?", "Proponer speedup reproducible frente al baseline escalar."),
        ("Primera optimizacion", "Priorizamos OpenMP por escenarios, AVX2 o CUDA?", "Perfilar primero; despues OpenMP por escenarios y luego kernel/AVX2 o CUDA."),
        ("Benchmark", "Usamos pasos fijos, trabajo real o las dos medidas?", "Reportar siempre tiempo, cell_updates, throughput y motivo de terminacion."),
        ("Profiling", "Que herramientas y particiones de MareNostrum tendremos?", "Elegir el profiler antes de tocar el kernel."),
        ("Reproducibilidad", "Esperais igualdad bit a bit entre CPU y GPU?", "Igualdad estricta dentro de cada backend; equivalencia funcional entre backends."),
        ("Modelo", "Basta el modelo educativo o conviene sensibilidad/correlacion espacial?", "Mantener el modelo estable y anadir sensibilidad solo si mejora la evaluacion."),
    ]
    story.append(make_table(question_rows, [31 * mm, 76 * mm, 67 * mm]))
    story.append(Spacer(1, 7 * mm))
    story.append(P("Frase util para encauzar la conversacion", "H2Custom"))
    story.append(info_box(
        "Propuesta de decision",
        "Queremos salir de la reunion con una metrica objetivo, una herramienta de perfilado, un hardware de referencia y una primera optimizacion acotada que podamos validar contra el baseline.",
        PALE_ORANGE, EMBER,
    ))
    story.append(Spacer(1, 5 * mm))
    story.append(P("No conviene prometer", "H2Custom"))
    for item in [
        "Prediccion operativa de incendios: el modelo es educativo.",
        "Comparacion directa entre los benchmarks actuales: mezclan maquinas y compiladores.",
        "Speedup AVX2 definitivo: la rama aun no esta integrada en main.",
        "CUDA inmediata: primero hay que congelar la referencia y el protocolo.",
    ]:
        story.append(bullet(item))
    story.append(PageBreak())

    story.append(P("3. Lectura tecnica: donde esta el trabajo interesante", "H1Custom"))
    story.append(P("Hotspot probable", "H2Custom"))
    story.append(P(
        "El coste dominante esta en el bucle de actualizacion temporal: cada celda visita hasta ocho vecinos, comprueba bordes, lee varios arrays SoA, aplica modificadores y calcula un hash para la decision probabilistica. El codigo esta en src/simulation.cpp, alrededor de los bucles de filas, columnas y vecinos.",
        "BodyCustom",
    ))
    for item in [
        "Perfilar si el limite real es ancho de banda, calculo, ramas o el hash determinista.",
        "Medir el coste de las comprobaciones de borde y valorar una ruta interior separada.",
        "Evitar optimizaciones que cambien la semantica sin compararlas con la referencia.",
        "Mantener la E/S fuera del tiempo de simulacion y registrar trabajo real ejecutado.",
    ]:
        story.append(bullet(item))
    story.append(P("Rutas de aceleracion", "H2Custom"))
    route_rows = [
        ("Ruta", "Ventaja", "Riesgo / condicion"),
        ("OpenMP por escenarios", "Cambio pequeno; escenarios independientes; buen primer control de escalabilidad.", "Necesita preservar el orden de resultados y medir saturacion de memoria."),
        ("AVX2", "Ataca el kernel de celdas y sirve como aprendizaje SIMD CPU.", "Debe integrarse en CMake/main y compararse en la misma maquina."),
        ("CUDA", "Una hebra por celda, arrays residentes y doble bufer encajan bien.", "Reducciones, divergencia, transferencia y diferencias flotantes."),
    ]
    story.append(make_table(route_rows, [34 * mm, 70 * mm, 70 * mm]))
    story.append(Spacer(1, 6 * mm))
    story.append(P("Puntos CUDA que ya estan preparados", "H2Custom"))
    for item in [
        "SoA y buffers current/next permiten intercambio de punteros en dispositivo.",
        "El hash sin estado evita RNG compartido y dependencias del orden.",
        "El kernel por celda puede leer el vecindario local y escribir un destino independiente.",
        "Las reducciones necesarias son: celdas Burning, celdas quemadas y deteccion de extincion.",
    ]:
        story.append(bullet(item))
    story.append(PageBreak())

    story.append(P("4. Plan de accion y demo", "H1Custom"))
    stage_rows = [
        ("Etapa", "Resultado", "Criterio de salida"),
        ("1. Congelar baseline", "Tag/commit, tests y benchmark en una sola maquina.", "Misma semilla y workload; resultados guardados con metadata."),
        ("2. Perfilar", "Hotspots y contadores de memoria/calculo.", "Decision basada en datos, no en intuicion."),
        ("3. OpenMP", "Escenarios paralelos con 1/2/4/8 hilos.", "Speedup y resultados no temporales equivalentes."),
        ("4. SIMD o CUDA", "Primer kernel optimizado.", "Comparacion contra baseline y explicacion de trade-offs."),
        ("5. Historia final", "Tabla de speedup, metodo y limites.", "Demo reproducible y candidatura defendible."),
    ]
    story.append(make_table(stage_rows, [36 * mm, 69 * mm, 69 * mm]))
    story.append(Spacer(1, 7 * mm))
    story.append(P("Demo Windows de dos minutos", "H2Custom"))
    story.append(P(
        "build\\Release\\ember.exe --width 256 --height 256 --steps 100 --scenarios 4 --seed 42 --wind-direction 45 --wind-strength 0.4 --base-spread 0.25 --output results\\mentor-demo --export both",
        "CodeCustom",
    ))
    for item in [
        "Ensenar la salida de throughput y los escenarios extinguidos/max_steps.",
        "Abrir summary.csv y una imagen scenario_000000_final.ppm.",
        "Repetir el comando con la misma semilla para mostrar reproducibilidad.",
        "Cerrar con una pregunta concreta: que optimizacion y profiler priorizamos?",
    ]:
        story.append(bullet(item))
    story.append(Spacer(1, 5 * mm))
    story.append(info_box(
        "Checklist para llevar",
        "Portatil con la build Release, copia del README, benchmark CSV, grafico simple de throughput por workload, y una lista de cuatro preguntas de decision. No hace falta llevar CUDA funcionando para que la reunion sea productiva.",
        PALE_BLUE, BLUE,
    ))
    story.append(Spacer(1, 8 * mm))
    story.append(P("Archivos de referencia", "H2Custom"))
    story.append(P(
        "README.md | src/simulation.cpp | include/ember/grid.hpp | include/ember/random.hpp | docs/model.md | docs/cuda-readiness.md | benchmarks/benchmark_results.csv",
        "SmallCustom",
    ))
    return story


def main():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    document = BaseDocTemplate(
        str(OUTPUT), pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm,
        topMargin=17 * mm, bottomMargin=20 * mm, title="EMBER - Estrategias para la reunion con mentores",
        author="EMBER",
    )
    frame = Frame(document.leftMargin, document.bottomMargin, document.width, document.height, id="normal")
    document.addPageTemplates([PageTemplate(id="main", frames=frame, onPage=footer)])
    document.build(build_story())
    print(OUTPUT)


if __name__ == "__main__":
    main()
