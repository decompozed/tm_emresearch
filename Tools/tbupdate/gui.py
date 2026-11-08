import os
import sys
from pathlib import Path

from PySide6.QtCore import QThread, Signal
from PySide6.QtWidgets import (QApplication, QFileDialog, QHBoxLayout, QLabel, QLineEdit, QMessageBox,
                               QPlainTextEdit, QPushButton, QVBoxLayout, QWidget)

from .package import BuildOptions, build_package, init_tree, product_root
from .tree import verify_tree
from .versions import GameVersion

FIELDS = [
    ('tree', 'Server tree:', True, 'Local copy of the update server tree (contains microvolts/patch.ini).'),
    ('files', 'New files:', True, 'Folder laid out like the client root: Bin/have_api.dll, data/ui.dat, ...'),
    ('client', 'Client root:', True, 'Installed client: baseline for init, warns about paths that do not exist.'),
    ('password', 'cgd password file:', False, 'Text file with the cgd.dip password (needed to set pi_version).'),
    ('version', 'Version (optional):', None, 'Explicit new version, e.g. ENG_2.0.7.5. Empty = last digit + 1.'),
]


class Worker(QThread):
    log = Signal(str)
    done = Signal(bool, str)

    def __init__(self, values):
        super().__init__()
        self.values = values

    def run(self):
        try:
            v = self.values
            tree = Path(v['tree'])
            client = Path(v['client']) if v['client'] else None
            if not (product_root(tree) / 'patch.ini').exists():
                if client is None:
                    raise ValueError('empty tree: set the client root to initialise it')
                init_tree(tree, client, self.log.emit)
            password = Path(v['password']).read_bytes().rstrip(b'\r\n') if v['password'] else None
            result = build_package(BuildOptions(
                files_dir=Path(v['files']), tree=tree, client_root=client, cgd_password=password,
                version=GameVersion.parse(v['version']) if v['version'] else None), self.log.emit)
            check = verify_tree(tree, result.source, password)
            for line in check.warnings:
                self.log.emit(f'(!) {line}')
            if check.errors:
                raise ValueError('\n'.join(check.errors))
            self.done.emit(True, f'{result.target} built: {result.cab}')
        except Exception as e:
            self.log.emit(f'(x) {e}')
            self.done.emit(False, str(e))


class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('ToyBattles update generator')
        self.resize(820, 520)
        layout = QVBoxLayout(self)
        self.edits = {}
        for key, label, is_dir, tip in FIELDS:
            row = QHBoxLayout()
            edit = QLineEdit()
            edit.setToolTip(tip)
            row.addWidget(QLabel(label))
            row.addWidget(edit)
            if is_dir is not None:
                button = QPushButton('Browse')
                button.clicked.connect(lambda _=False, e=edit, d=is_dir: self.browse(e, d))
                row.addWidget(button)
            layout.addLayout(row)
            self.edits[key] = edit
        self.run_button = QPushButton('Build next version')
        self.run_button.clicked.connect(self.start)
        layout.addWidget(self.run_button)
        self.output = QPlainTextEdit()
        self.output.setReadOnly(True)
        layout.addWidget(self.output)
        self.worker = None

    def browse(self, edit, is_dir):
        if is_dir:
            path = QFileDialog.getExistingDirectory(self, 'Select folder', os.getcwd())
        else:
            path, _ = QFileDialog.getOpenFileName(self, 'Select file', os.getcwd())
        if path:
            edit.setText(path)

    def start(self):
        values = {k: e.text().strip() for k, e in self.edits.items()}
        if not values['tree'] or not values['files']:
            QMessageBox.warning(self, 'Missing values', 'Server tree and new files are required.')
            return
        self.run_button.setEnabled(False)
        self.worker = Worker(values)
        self.worker.log.connect(self.output.appendPlainText)
        self.worker.done.connect(self.finished)
        self.worker.start()

    def finished(self, ok, message):
        self.run_button.setEnabled(True)
        (QMessageBox.information if ok else QMessageBox.critical)(self, 'Done' if ok else 'Error', message)


def run() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()
