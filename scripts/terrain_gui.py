#!/usr/bin/env python3
"""Tkinter launcher for the EMBER real-terrain demo.

Drives the existing tools without modifying them: builds ``ember`` with CMake,
runs the simulation, calls ``tools/terrain/render.py`` and shows the resulting
PNG. ``tools/terrain/prepare.py`` and ``render.py`` are only invoked as
subprocesses, never edited or replaced.
"""
from __future__ import annotations

import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText

APP_TITLE = "EMBER · Terreno real"
CONFIG_FILENAME = "terrain_gui_config.json"
REPO_MARKERS = (Path("apps/CMakeLists.txt"), Path("tools/terrain/render.py"))
ZENODO_SOURCE = "https://zenodo.org/api/records/18788338/files/ESP_4326_ZAFM.tif/content"


def app_base_dir() -> Path:
    # PyInstaller's onefile build has no meaningful __file__; anchor on the exe instead.
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def find_repo_root(start: Path) -> Path | None:
    for candidate in (start, *start.parents):
        if all((candidate / marker).is_file() for marker in REPO_MARKERS):
            return candidate
    return None


def config_path() -> Path:
    return app_base_dir() / CONFIG_FILENAME


def load_config() -> dict:
    try:
        return json.loads(config_path().read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_config(data: dict) -> None:
    try:
        config_path().write_text(json.dumps(data, indent=2), encoding="utf-8")
    except OSError:
        pass


def default_python(root: Path) -> str:
    venv_python = root / ".venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if venv_python.is_file():
        return str(venv_python)
    if not getattr(sys, "frozen", False):
        return sys.executable
    for name in ("py", "python", "python3"):
        found = shutil.which(name)
        if found:
            return found
    return "python"


def find_ember_executable(root: Path) -> Path | None:
    build_dir = root / "build"
    for candidate in (
        build_dir / "Release" / "ember.exe",
        build_dir / "ember.exe",
        build_dir / "Release" / "ember",
        build_dir / "ember",
    ):
        if candidate.is_file():
            return candidate
    return None


def build_configure_command(root: Path) -> list[str]:
    return [
        "cmake", "-S", str(root), "-B", str(root / "build"),
        "-DCMAKE_BUILD_TYPE=Release", "-DEMBER_ENABLE_MPI=OFF",
        "-DEMBER_ENABLE_CUDA=OFF", "-DAVX2=OFF",
    ]


def build_build_command(root: Path) -> list[str]:
    return ["cmake", "--build", str(root / "build"), "--config", "Release", "--parallel"]


def build_simulate_command(exe: Path, params: dict) -> list[str]:
    return [
        str(exe), "--terrain", params["terrain"], "--steps", str(params["steps"]),
        "--scenarios", str(params["scenarios"]), "--seed", str(params["seed"]),
        "--terrain-fuel", str(params["fuel"]), "--terrain-moisture", str(params["moisture"]),
        "--output", params["output"], "--export", "csv",
    ]


def build_render_command(python_exe: str, root: Path, output_dir: str, scenario: int) -> list[str]:
    return [python_exe, str(root / "tools/terrain/render.py"), output_dir, "--scenario", str(scenario)]


def build_prepare_command(python_exe: str, root: Path, params: dict) -> list[str]:
    return [
        python_exe, str(root / "tools/terrain/prepare.py"),
        "--source", params["source"], "--output", params["output"],
        "--longitude", str(params["longitude"]), "--latitude", str(params["latitude"]),
        "--size", str(params["size"]), "--resolution", str(params["resolution"]),
    ]


class CommandRunner:
    """Runs one subprocess and streams its combined stdout/stderr into a queue."""

    def __init__(self, log_queue: "queue.Queue[tuple[str, object]]") -> None:
        self.log_queue = log_queue

    def run(self, command: list[str], cwd: Path | None = None) -> int:
        self.log_queue.put(("line", "$ " + " ".join(str(part) for part in command)))
        try:
            process = subprocess.Popen(
                [str(part) for part in command],
                cwd=str(cwd) if cwd else None,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            )
        except OSError as error:
            self.log_queue.put(("line", f"No se pudo iniciar: {error}"))
            return -1
        assert process.stdout is not None
        for line in process.stdout:
            self.log_queue.put(("line", line.rstrip()))
        return process.wait()


class App:
    def __init__(self) -> None:
        self.config_data = load_config()
        self.root = tk.Tk()
        self.root.title(APP_TITLE)
        self.root.geometry("1060x760")
        self.root.minsize(880, 620)

        self.log_queue: "queue.Queue[tuple[str, object]]" = queue.Queue()
        self.runner = CommandRunner(self.log_queue)
        self.worker_thread: threading.Thread | None = None
        self.photo_image: tk.PhotoImage | None = None
        self.last_image_path: Path | None = None
        self.last_output_dir: Path | None = None

        detected_root = find_repo_root(app_base_dir()) or Path.cwd()
        self.root_var = tk.StringVar(value=self.config_data.get("root", str(detected_root)))
        self.python_var = tk.StringVar(
            value=self.config_data.get("python", default_python(Path(self.root_var.get())))
        )

        self.terrain_var = tk.StringVar(value=self.config_data.get("terrain", "data/terrain/collserola.asc"))
        self.steps_var = tk.StringVar(value=self.config_data.get("steps", "500"))
        self.scenarios_var = tk.StringVar(value=self.config_data.get("scenarios", "2"))
        self.seed_var = tk.StringVar(value=self.config_data.get("seed", "42"))
        self.fuel_var = tk.StringVar(value=self.config_data.get("fuel", "1.0"))
        self.moisture_var = tk.StringVar(value=self.config_data.get("moisture", "0.2"))
        self.output_var = tk.StringVar(value=self.config_data.get("output", "out/collserola"))
        self.scenario_render_var = tk.StringVar(value=self.config_data.get("scenario_render", "0"))

        self.do_prepare_var = tk.BooleanVar(value=self.config_data.get("do_prepare", False))
        self.prepare_source_var = tk.StringVar(value=self.config_data.get("prepare_source", ZENODO_SOURCE))
        self.prepare_lon_var = tk.StringVar(value=self.config_data.get("prepare_lon", "2.10"))
        self.prepare_lat_var = tk.StringVar(value=self.config_data.get("prepare_lat", "41.45"))
        self.prepare_size_var = tk.StringVar(value=self.config_data.get("prepare_size", "512"))
        self.prepare_resolution_var = tk.StringVar(value=self.config_data.get("prepare_resolution", "10.0"))

        self.status_var = tk.StringVar(value="Listo.")
        self.env_status_var = tk.StringVar(value="")

        self._build_ui()
        self._refresh_env_status()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(80, self._poll_queue)

    # ---- UI construction -------------------------------------------------

    def _build_ui(self) -> None:
        notebook = ttk.Notebook(self.root)
        notebook.pack(fill="x", padx=8, pady=(8, 4))

        sim_tab = ttk.Frame(notebook, padding=10)
        config_tab = ttk.Frame(notebook, padding=10)
        notebook.add(sim_tab, text="Simulación")
        notebook.add(config_tab, text="Configuración")

        self._build_sim_tab(sim_tab)
        self._build_config_tab(config_tab)

        actions = ttk.Frame(self.root, padding=(8, 0))
        actions.pack(fill="x")
        self.run_button = ttk.Button(
            actions, text="▶ Ejecutar todo (simular + render)", command=self._start_full_pipeline)
        self.run_button.pack(side="left")
        self.build_button = ttk.Button(actions, text="Recompilar EMBER", command=self._start_build_only)
        self.build_button.pack(side="left", padx=6)
        self.open_output_button = ttk.Button(
            actions, text="Abrir carpeta de resultados", command=self._open_output_folder, state="disabled")
        self.open_output_button.pack(side="left", padx=6)
        self.save_image_button = ttk.Button(
            actions, text="Guardar imagen como…", command=self._save_image_as, state="disabled")
        self.save_image_button.pack(side="left", padx=6)

        self.progress = ttk.Progressbar(self.root, mode="indeterminate")
        self.progress.pack(fill="x", padx=8, pady=(6, 0))

        ttk.Label(self.root, textvariable=self.status_var, anchor="w").pack(fill="x", padx=8, pady=(2, 6))

        body = ttk.Panedwindow(self.root, orient="horizontal")
        body.pack(fill="both", expand=True, padx=8, pady=(0, 8))

        log_frame = ttk.Labelframe(body, text="Registro", padding=6)
        self.log_widget = ScrolledText(log_frame, width=52, height=20, state="disabled", font=("Consolas", 9))
        self.log_widget.pack(fill="both", expand=True)
        body.add(log_frame, weight=1)

        image_frame = ttk.Labelframe(body, text="Resultado", padding=6)
        self.image_label = ttk.Label(
            image_frame, text="Todavía no se ha generado ninguna imagen.", anchor="center")
        self.image_label.pack(fill="both", expand=True)
        body.add(image_frame, weight=2)

    def _build_sim_tab(self, parent: ttk.Frame) -> None:
        row = 0

        def add_field(label: str, var: tk.StringVar, width: int = 18) -> None:
            nonlocal row
            ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=2)
            ttk.Entry(parent, textvariable=var, width=width).grid(
                row=row, column=1, sticky="w", pady=2, padx=(6, 0))
            row += 1

        ttk.Label(parent, text="Terreno (.asc)").grid(row=row, column=0, sticky="w", pady=2)
        ttk.Entry(parent, textvariable=self.terrain_var, width=48).grid(
            row=row, column=1, columnspan=2, sticky="we", pady=2, padx=(6, 0))
        ttk.Button(parent, text="Examinar…", command=self._browse_terrain).grid(row=row, column=3, padx=(6, 0))
        row += 1

        add_field("Pasos (--steps)", self.steps_var)
        add_field("Escenarios (--scenarios)", self.scenarios_var)
        add_field("Semilla (--seed)", self.seed_var)
        add_field("Combustible (--terrain-fuel)", self.fuel_var)
        add_field("Humedad (--terrain-moisture)", self.moisture_var)

        ttk.Label(parent, text="Carpeta de salida").grid(row=row, column=0, sticky="w", pady=2)
        ttk.Entry(parent, textvariable=self.output_var, width=48).grid(
            row=row, column=1, columnspan=2, sticky="we", pady=2, padx=(6, 0))
        ttk.Button(parent, text="Examinar…", command=self._browse_output).grid(row=row, column=3, padx=(6, 0))
        row += 1

        add_field("Escenario a renderizar", self.scenario_render_var, width=6)

        ttk.Separator(parent, orient="horizontal").grid(row=row, column=0, columnspan=4, sticky="we", pady=8)
        row += 1

        ttk.Checkbutton(
            parent, variable=self.do_prepare_var,
            text="Preparar terreno antes de simular (descarga ZAFM; requiere internet y rasterio)",
        ).grid(row=row, column=0, columnspan=4, sticky="w")
        row += 1

        prepare_frame = ttk.Labelframe(
            parent, text="Parámetros de preparación (tools/terrain/prepare.py)", padding=8)
        prepare_frame.grid(row=row, column=0, columnspan=4, sticky="we", pady=(4, 0))

        ttk.Label(prepare_frame, text="Origen (URL o GeoTIFF local)").grid(row=0, column=0, sticky="w")
        ttk.Entry(prepare_frame, textvariable=self.prepare_source_var, width=50).grid(
            row=0, column=1, columnspan=3, sticky="we", padx=(6, 0))
        ttk.Button(prepare_frame, text="Examinar…", command=self._browse_prepare_source).grid(
            row=0, column=4, padx=(6, 0))

        ttk.Label(prepare_frame, text="Longitud").grid(row=1, column=0, sticky="w", pady=(4, 0))
        ttk.Entry(prepare_frame, textvariable=self.prepare_lon_var, width=10).grid(
            row=1, column=1, sticky="w", pady=(4, 0))
        ttk.Label(prepare_frame, text="Latitud").grid(row=1, column=2, sticky="w", pady=(4, 0))
        ttk.Entry(prepare_frame, textvariable=self.prepare_lat_var, width=10).grid(
            row=1, column=3, sticky="w", pady=(4, 0))

        ttk.Label(prepare_frame, text="Tamaño (celdas)").grid(row=2, column=0, sticky="w", pady=(4, 0))
        ttk.Entry(prepare_frame, textvariable=self.prepare_size_var, width=10).grid(
            row=2, column=1, sticky="w", pady=(4, 0))
        ttk.Label(prepare_frame, text="Resolución (m)").grid(row=2, column=2, sticky="w", pady=(4, 0))
        ttk.Entry(prepare_frame, textvariable=self.prepare_resolution_var, width=10).grid(
            row=2, column=3, sticky="w", pady=(4, 0))

        parent.columnconfigure(1, weight=1)

    def _build_config_tab(self, parent: ttk.Frame) -> None:
        ttk.Label(parent, text="Carpeta del proyecto EMBER").grid(row=0, column=0, sticky="w")
        ttk.Entry(parent, textvariable=self.root_var, width=56).grid(
            row=0, column=1, sticky="we", padx=(6, 0))
        ttk.Button(parent, text="Examinar…", command=self._browse_root).grid(row=0, column=2, padx=(6, 0))

        ttk.Label(parent, text="Intérprete de Python").grid(row=1, column=0, sticky="w", pady=(6, 0))
        ttk.Entry(parent, textvariable=self.python_var, width=56).grid(
            row=1, column=1, sticky="we", padx=(6, 0), pady=(6, 0))
        ttk.Button(parent, text="Examinar…", command=self._browse_python).grid(
            row=1, column=2, padx=(6, 0), pady=(6, 0))

        ttk.Button(parent, text="Detectar automáticamente", command=self._detect_environment).grid(
            row=2, column=1, sticky="w", pady=8)
        ttk.Button(
            parent, text="Instalar dependencias Python (numpy, matplotlib…)",
            command=self._start_install_dependencies,
        ).grid(row=3, column=1, sticky="w")

        ttk.Label(parent, textvariable=self.env_status_var, foreground="#555", justify="left").grid(
            row=4, column=0, columnspan=3, sticky="w", pady=(10, 0))

        parent.columnconfigure(1, weight=1)

    # ---- browse handlers ---------------------------------------------------

    def _browse_root(self) -> None:
        chosen = filedialog.askdirectory(initialdir=self.root_var.get() or ".")
        if chosen:
            self.root_var.set(chosen)
            self._detect_environment()

    def _browse_python(self) -> None:
        chosen = filedialog.askopenfilename(title="Selecciona python.exe")
        if chosen:
            self.python_var.set(chosen)

    def _browse_terrain(self) -> None:
        chosen = filedialog.askopenfilename(
            title="Selecciona un archivo .asc", filetypes=[("ESRI ASCII Grid", "*.asc"), ("Todos", "*.*")])
        if chosen:
            self.terrain_var.set(chosen)

    def _browse_output(self) -> None:
        chosen = filedialog.askdirectory(title="Selecciona la carpeta de salida")
        if chosen:
            self.output_var.set(chosen)

    def _browse_prepare_source(self) -> None:
        chosen = filedialog.askopenfilename(
            title="Selecciona un GeoTIFF local (opcional)",
            filetypes=[("GeoTIFF", "*.tif *.tiff"), ("Todos", "*.*")])
        if chosen:
            self.prepare_source_var.set(chosen)

    def _detect_environment(self) -> None:
        root = Path(self.root_var.get())
        self.python_var.set(default_python(root))
        self._refresh_env_status()

    def _refresh_env_status(self) -> None:
        root = Path(self.root_var.get())
        exe = find_ember_executable(root)
        markers_ok = all((root / marker).is_file() for marker in REPO_MARKERS)
        self.env_status_var.set(
            f"Proyecto válido: {'sí' if markers_ok else 'NO (revisa la carpeta seleccionada)'}\n"
            f"ember compilado: {exe if exe else 'no encontrado (se compilará al pulsar Ejecutar todo)'}"
        )

    # ---- logging / param collection ---------------------------------------

    def _append_log(self, text: str) -> None:
        self.log_widget.configure(state="normal")
        self.log_widget.insert("end", text + "\n")
        self.log_widget.see("end")
        self.log_widget.configure(state="disabled")

    def _collect_sim_params(self) -> dict:
        try:
            return {
                "terrain": self.terrain_var.get().strip(),
                "steps": int(self.steps_var.get()),
                "scenarios": int(self.scenarios_var.get()),
                "seed": int(self.seed_var.get()),
                "fuel": float(self.fuel_var.get()),
                "moisture": float(self.moisture_var.get()),
                "output": self.output_var.get().strip(),
                "scenario_render": int(self.scenario_render_var.get()),
            }
        except ValueError as error:
            raise ValueError(f"Parámetro de simulación no válido: {error}") from error

    def _collect_prepare_params(self) -> dict:
        try:
            return {
                "source": self.prepare_source_var.get().strip(),
                "output": self.terrain_var.get().strip(),
                "longitude": float(self.prepare_lon_var.get()),
                "latitude": float(self.prepare_lat_var.get()),
                "size": int(self.prepare_size_var.get()),
                "resolution": float(self.prepare_resolution_var.get()),
            }
        except ValueError as error:
            raise ValueError(f"Parámetro de preparación no válido: {error}") from error

    def _save_current_config(self) -> None:
        save_config({
            "root": self.root_var.get(), "python": self.python_var.get(),
            "terrain": self.terrain_var.get(), "steps": self.steps_var.get(),
            "scenarios": self.scenarios_var.get(), "seed": self.seed_var.get(),
            "fuel": self.fuel_var.get(), "moisture": self.moisture_var.get(),
            "output": self.output_var.get(), "scenario_render": self.scenario_render_var.get(),
            "do_prepare": self.do_prepare_var.get(), "prepare_source": self.prepare_source_var.get(),
            "prepare_lon": self.prepare_lon_var.get(), "prepare_lat": self.prepare_lat_var.get(),
            "prepare_size": self.prepare_size_var.get(),
            "prepare_resolution": self.prepare_resolution_var.get(),
        })

    # ---- pipeline execution -------------------------------------------------

    def _start_full_pipeline(self) -> None:
        self._start_pipeline(do_prepare=self.do_prepare_var.get(), force_build=False, do_simulate=True)

    def _start_build_only(self) -> None:
        self._start_pipeline(do_prepare=False, force_build=True, do_simulate=False)

    def _start_pipeline(self, do_prepare: bool, force_build: bool, do_simulate: bool) -> None:
        if self.worker_thread and self.worker_thread.is_alive():
            return
        try:
            root = Path(self.root_var.get()).resolve()
            python_exe = self.python_var.get().strip() or default_python(root)
            sim_params = self._collect_sim_params()
            prepare_params = self._collect_prepare_params() if do_prepare else None
        except ValueError as error:
            messagebox.showerror(APP_TITLE, str(error))
            return

        if not all((root / marker).is_file() for marker in REPO_MARKERS):
            messagebox.showerror(APP_TITLE, f"No parece la carpeta del proyecto EMBER:\n{root}")
            return

        self._save_current_config()
        self._set_running(True)
        self.log_widget.configure(state="normal")
        self.log_widget.delete("1.0", "end")
        self.log_widget.configure(state="disabled")

        self.worker_thread = threading.Thread(
            target=self._pipeline_worker,
            args=(root, python_exe, do_prepare, prepare_params, force_build, do_simulate, sim_params),
            daemon=True,
        )
        self.worker_thread.start()

    def _pipeline_worker(
        self, root: Path, python_exe: str, do_prepare: bool, prepare_params: dict | None,
        force_build: bool, do_simulate: bool, sim_params: dict,
    ) -> None:
        try:
            if do_prepare:
                self.log_queue.put(("status", "Preparando terreno (requiere internet)…"))
                code = self.runner.run(build_prepare_command(python_exe, root, prepare_params), cwd=root)
                if code != 0:
                    self.log_queue.put(("failed", f"La preparación falló (código {code})."))
                    return

            exe = find_ember_executable(root)
            if force_build or exe is None:
                self.log_queue.put(("status", "Compilando EMBER con CMake…"))
                if self.runner.run(build_configure_command(root), cwd=root) != 0:
                    self.log_queue.put((
                        "failed",
                        "cmake configure falló. Instala CMake y un compilador de C++ "
                        "(pestaña Configuración).",
                    ))
                    return
                if self.runner.run(build_build_command(root), cwd=root) != 0:
                    self.log_queue.put(("failed", "La compilación falló; revisa el registro."))
                    return
                exe = find_ember_executable(root)
                if exe is None:
                    self.log_queue.put(("failed", "Compilación terminada pero no se encontró ember."))
                    return

            if not do_simulate:
                self.log_queue.put(("generic_done", "Compilación completada."))
                return

            output_dir = Path(sim_params["output"])
            if not output_dir.is_absolute():
                output_dir = root / output_dir

            self.log_queue.put(("status", "Ejecutando la simulación…"))
            if self.runner.run(build_simulate_command(exe, sim_params), cwd=root) != 0:
                self.log_queue.put(("failed", "La simulación terminó con error; revisa el registro."))
                return

            self.log_queue.put(("status", "Generando la imagen…"))
            render_code = self.runner.run(
                build_render_command(python_exe, root, str(output_dir), sim_params["scenario_render"]),
                cwd=root,
            )
            if render_code != 0:
                self.log_queue.put((
                    "failed",
                    "El renderizado falló. ¿Están instalados numpy y matplotlib? (pestaña Configuración)",
                ))
                return

            png_path = output_dir / f"scenario_{sim_params['scenario_render']:06d}.png"
            self.log_queue.put(("sim_done", (str(output_dir), str(png_path))))
        except Exception as error:  # surfaced in the UI instead of dying silently in the thread
            self.log_queue.put(("failed", f"Error inesperado: {error}"))

    def _start_install_dependencies(self) -> None:
        if self.worker_thread and self.worker_thread.is_alive():
            return
        root = Path(self.root_var.get()).resolve()
        self._set_running(True)
        self.status_var.set("Instalando dependencias Python…")
        self.worker_thread = threading.Thread(
            target=self._install_dependencies_worker, args=(root,), daemon=True)
        self.worker_thread.start()

    def _install_dependencies_worker(self, root: Path) -> None:
        try:
            venv_dir = root / ".venv"
            venv_python = venv_dir / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
            if not venv_python.is_file():
                self.log_queue.put(("status", "Creando entorno virtual (.venv)…"))
                if self.runner.run([sys.executable, "-m", "venv", str(venv_dir)], cwd=root) != 0:
                    self.log_queue.put(("failed", "No se pudo crear el entorno virtual."))
                    return
            self.log_queue.put(("status", "Instalando numpy y matplotlib…"))
            self.runner.run([str(venv_python), "-m", "pip", "install", "--upgrade", "pip"], cwd=root)
            if self.runner.run([str(venv_python), "-m", "pip", "install", "numpy", "matplotlib"], cwd=root) != 0:
                self.log_queue.put(("failed", "No se pudieron instalar numpy/matplotlib."))
                return
            self.log_queue.put(("line", "Intentando instalar rasterio (solo necesario para 'Preparar terreno')…"))
            if self.runner.run([str(venv_python), "-m", "pip", "install", "rasterio"], cwd=root) != 0:
                self.log_queue.put((
                    "line",
                    "rasterio no se pudo instalar; 'Preparar terreno' no estará disponible, pero "
                    "simular y renderizar con el terreno incluido funcionará igualmente.",
                ))
            self.python_var.set(str(venv_python))
            self.log_queue.put(("generic_done", "Dependencias instaladas."))
        except Exception as error:
            self.log_queue.put(("failed", f"Error inesperado: {error}"))

    # ---- queue polling / UI state -------------------------------------------

    def _poll_queue(self) -> None:
        try:
            while True:
                kind, payload = self.log_queue.get_nowait()
                if kind == "line":
                    self._append_log(str(payload))
                elif kind == "status":
                    self.status_var.set(str(payload))
                elif kind == "failed":
                    self._finish_run(success=False, message=str(payload))
                elif kind == "sim_done":
                    output_dir, png_path = payload
                    self._finish_run(success=True, output_dir=output_dir, image_path=png_path)
                elif kind == "generic_done":
                    self._finish_run(success=True, message=str(payload))
        except queue.Empty:
            pass
        self.root.after(80, self._poll_queue)

    def _finish_run(
        self, success: bool, message: str | None = None,
        output_dir: str | None = None, image_path: str | None = None,
    ) -> None:
        self._set_running(False)
        if output_dir:
            self.last_output_dir = Path(output_dir)
            self.open_output_button.configure(state="normal")
        if success:
            self.status_var.set(message or "Completado.")
            if image_path and Path(image_path).is_file():
                self._show_image(Path(image_path))
        else:
            self.status_var.set("Error.")
            messagebox.showerror(APP_TITLE, message or "Ocurrió un error.")
        self._refresh_env_status()

    def _set_running(self, running: bool) -> None:
        state = "disabled" if running else "normal"
        self.run_button.configure(state=state)
        self.build_button.configure(state=state)
        if running:
            self.progress.start(12)
        else:
            self.progress.stop()

    def _show_image(self, path: Path) -> None:
        try:
            image = tk.PhotoImage(file=str(path))
        except tk.TclError as error:
            self._append_log(f"No se pudo cargar la imagen: {error}")
            return
        max_width = 900
        factor = max(1, -(-image.width() // max_width))  # ceil division, integer-only subsample
        if factor > 1:
            image = image.subsample(factor, factor)
        self.photo_image = image
        self.image_label.configure(image=image, text="")
        self.last_image_path = path
        self.save_image_button.configure(state="normal")

    def _open_output_folder(self) -> None:
        if self.last_output_dir and self.last_output_dir.is_dir():
            os.startfile(str(self.last_output_dir))  # noqa: S606 - Windows-only launcher

    def _save_image_as(self) -> None:
        if not self.last_image_path:
            return
        chosen = filedialog.asksaveasfilename(defaultextension=".png", filetypes=[("PNG", "*.png")])
        if chosen:
            shutil.copyfile(self.last_image_path, chosen)

    def _on_close(self) -> None:
        if self.worker_thread and self.worker_thread.is_alive():
            if not messagebox.askyesno(APP_TITLE, "Hay un proceso en curso. ¿Cerrar de todas formas?"):
                return
        self._save_current_config()
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


def main() -> None:
    App().run()


if __name__ == "__main__":
    main()
