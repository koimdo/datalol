#!/usr/bin/python3

import subprocess
import socket
import json
import argparse
import sys
import os

from PyQt5 import QtCore
from PyQt5.QtCore import QSize, Qt
from PyQt5.QtWidgets import (
    QApplication, QWidget, QMainWindow,
    QLabel, QMenu, QToolBar, QAction, QStatusBar, QMessageBox,
    QPushButton, QListView, QLineEdit,
    QVBoxLayout, QHBoxLayout, QAbstractItemView,
    QPlainTextEdit, QSplitter
)
from PyQt5.QtGui import (
    QIcon, QKeySequence, QTextCursor
)

from PyQt5.QtCore import pyqtSignal, QThread


class Error(Exception):
    pass

class Client:
    def __init__(self, fileno):
        self.fdr = fdr
        self.fdw = fdw
        self.callid = 0

    def _call(self, method, *args, **kwargs):

        assert(not(args and kwargs))
        d = {
            'method': method,
            'id': self.callid,
            'params': args or kwargs or [],
        }

        self._write(json.dumps(d))

        # Read response:
        response = json.loads(self._read())
        assert(response['id'] == self.callid)

        self.callid += 1
        if 'result' in response:
            return response['result']
        else:
            err = response['error']
            raise Error(err['code'], err['message'], err.get('data', None))

    def _write(self, sval):
        print("_write:", repr(sval))
        for pos in range(0, len(sval), 4096):
            line = sval[pos:pos+4096]
            self.fdw.write("{:04X}{}".format(len(line)+4, line))
        self.fdw.write('0000')
        self.fdw.flush()

    def _read(self):
        lines = []
        while True:
            length = int(self.fdr.read(4), base=16)
            if not length:
                break
            lines.append(self.fdr.read(length-4))

        return ''.join(lines)

    def help(self):
        print(self._call('help'))

    def loadQueries(self):
        return self._call('loadQueries')

    def getCurrent(self):
        return self._call('getCurrent')

    def setBreakpoint(self, qid, tripCount):
        self._call('break', qid, tripCount)

    def resume(self):
        self._call('resume')

    def describe(self):
        return self._call('describe')

    def readTable(self, table):
        return self._call('readTable', table)

def run(args):
    (mysocket, sub_fd) = socket.socketpair()
    sub_env = dict(os.environ)
    sub_env['LOLBERT_FD'] = str(sub_fd.fileno())
    sub = subprocess.Popen(args, env = sub_env, pass_fds=[sub_fd.fileno()])

    return Client(mysocket.makefile('r'), mysocket.makefile('w'))

class QueriesModel(QtCore.QAbstractListModel):
    def __init__(self, items, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.items = items

    def data(self, index, role):
        if role == Qt.DisplayRole:
            item = self.items[index.row()]

            return "{}:{}:{}".format(item['file'], item['function'], item['line'])

    def rowCount(self, index):
        return len(self.items)

class MainWindow(QMainWindow):
    def __init__(self, client):
        super().__init__()

        self.client = client
        self.setWindowTitle("LOLBert")

        self.listmodel = QueriesModel(client.loadQueries())
        self.itemview = QListView()
        self.itemview.doubleClicked.connect(self.show_query)
        self.itemview.setModel(self.listmodel)

        self.entry = QLineEdit()
        self.enter = QPushButton("Enter")

        layout1 = QVBoxLayout()
        layout1.addWidget(self.entry)
        layout1.addWidget(self.itemview)

        lhs = QWidget()
        lhs.setLayout(layout1)

        self.rhs = QPlainTextEdit()
        split = QSplitter()
        split.addWidget(lhs)
        split.addWidget(self.rhs)

        toolbar = QToolBar("Main toolbar")
        self.addToolBar(toolbar)
        statusbar = QStatusBar(self)
        self.setStatusBar(statusbar)

        resume = QAction(QIcon.fromTheme('play'), "Resume", self)
        resume.setStatusTip("Resume program")
        resume.triggered.connect(self.do_resume)
        toolbar.addAction(resume)

        menu = self.menuBar()
        file_menu = menu.addMenu("&File")

        quit_action = QAction(QIcon.fromTheme('quit'), "Quit", self)
        quit_action.setStatusTip("Quit program")
        quit_action.setShortcut(QKeySequence("Ctrl+q"))
        quit_action.triggered.connect(self.quit_app)

        file_menu.addSeparator()
        file_menu.addAction(quit_action)

        self.setCentralWidget(split)

    def selected_query(self, idx = None):
        if idx is None:
            selection = self.itemview.selectedIndexes()
            if selection:
                idx = selection[0]
        if idx is None:
            return None
        else:
            return self.listmodel.items[idx.row()]

    def show_query(self, idx):
        query = self.selected_query(idx)
        text = None
        with open(query['file'], 'r') as f:
            text = f.read()
        assert(text is not None)
        self.rhs.setPlainText(text)
        cursor = QTextCursor(self.rhs.document().findBlockByLineNumber(query['line']))
        self.rhs.setTextCursor(cursor)
        cursor.select(QTextCursor.LineUnderCursor);

    def do_resume(self):
        self.client.resume()
    def quit_app(self):
        QApplication.exit(0)

def main():
    parser = argparse.ArgumentParser(prog='interp test',
                    description='Test for simple interpreter')
    parser.add_argument('--verbose', dest='verbose', action='store_const',
                        const=True, default=False,
                        help='debug output')
    parser.add_argument('--args', nargs=argparse.REMAINDER, help="Program and arguments to run")
    args = parser.parse_args()

    print(args)

    client = run(args.args)

    app = QApplication(sys.argv)
    window = MainWindow(client)
    window.show()

    return app.exec()

if __name__ == '__main__':
    sys.exit(main())
