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

from PyQt5.QtCore import QTimer
from PyQt5.QtNetwork import QLocalSocket

import signal

class Error(Exception):
    pass

class Client:
    def __init__(self, sock):
        self._socket = sock      # Keep reference so it's not closed
        self.socket = QLocalSocket()
        res = self.socket.setSocketDescriptor(sock.fileno())
        self.socket.readyRead.connect(self.readyRead)
        #self.socket.bytesWritten.connect(self.bytesWritten)
        self.socket.errorOccurred.connect(self.errorOccured)
        self.socket.readChannelFinished.connect(self.finished)

        self.callid = 0
        self.in_progress = {}
        self.notifications = {}

    def errorOccured(self):
        print("Error!")

    def bytesWritten(self, nbytes):
        print("Written: ", nbytes)

    def readyRead(self):
        while True:
            pkt = self._read()
            if pkt is None:
                break
            response = json.loads(pkt)
            if 'id' not in response:
                # a notification
                handler = self.notifications[response['method']]
                handler(response['params'])
            else:
                handler = self.in_progress.pop(response['id'])
                if 'result' in response:
                    handler(response['result'])
                else:
                    err = response['error']
                    raise Error(err['code'], err['message'], err.get('data', None))

    def finished(self):
        print("Channel closed")

    def register_notification(self, method, handler):
        self.notifications[method] = handler

    def request(self, method, *args, response=None, **kwargs):
        assert(not(args and kwargs))
        
        d = {
            'method': method,
            'params': args or kwargs or [],
        }

        if response is not None:
            d['id'] = self.callid
            self.in_progress[self.callid] = response
            self.callid += 1

        self._write(d)

    def _write(self, val):
        sval = json.dumps(val)
        for pos in range(0, len(sval), 4096):
            line = sval[pos:pos+4096]
            self.socket.write("{:04X}{}".format(len(line)+4, line).encode())
        self.socket.write('0000'.encode())

    def _read_exactly(self, size):
        bs = self.socket.read(size)
        if len(bs) < size:
            self.socket.rollbackTransaction()
            return None
        return bs

    def _read(self):
        self.socket.startTransaction()
        lines = []
        while True:
            pktlen = self._read_exactly(4)
            if pktlen is None:
                return None
            length = int(pktlen, base=16)
            if not length:
                break
            more = self._read_exactly(length-4)
            if more is None:
                return None
            lines.append(more)

        self.socket.commitTransaction()
        res = b''.join(lines)
        return res

    def help(self):
        print(self._call('help'))

class QueriesModel(QtCore.QAbstractListModel):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.items = []

    def populate(self, items):
        self.items = items
        self.layoutChanged.emit()

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

        self.listmodel = QueriesModel()
        self.client.request('loadQueries', response=self.listmodel.populate)
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

        resume = QAction(# QIcon.fromTheme('play'),
                         "Resume", self)
        resume.setStatusTip("Resume program")
        resume.triggered.connect(lambda: self.client.request('resume'))
        toolbar.addAction(resume)

        menu = self.menuBar()
        file_menu = menu.addMenu("&File")

        quit_action = QAction(# QIcon.fromTheme('quit'),
                              "Quit", self)
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

    def quit_app(self):
        QApplication.exit(0)

def sigint_handler(*args):
    QApplication.quit()

class LolbertApp(QApplication):
    def __init__(self, args):
        super().__init__(sys.argv)
        self.args = args
        QTimer.singleShot(0, self.start)

    def hello(self, params):
        print("Hello!", params)
    def _runprog(self):
        (mysocket, sub_fd) = socket.socketpair()
        sub_env = dict(os.environ)
        sub_env['LOLBERT_FD'] = str(sub_fd.fileno())
        self.client = Client(mysocket)
        self.client.register_notification('Hello', self.hello)
        self.inferior = subprocess.Popen(self.args.args, env = sub_env, pass_fds=[sub_fd.fileno()])

    def start(self):
        self._runprog()
        self.window = MainWindow(self.client)
        self.window.show()

def main():
    parser = argparse.ArgumentParser(prog='interp test',
                    description='Test for simple interpreter')
    parser.add_argument('--verbose', dest='verbose', action='store_const',
                        const=True, default=False,
                        help='debug output')
    parser.add_argument('--args', nargs=argparse.REMAINDER, help="Program and arguments to run")
    args = parser.parse_args()

    signal.signal(signal.SIGINT, sigint_handler)
    app = LolbertApp(args)
    return app.exec()

if __name__ == '__main__':
    sys.exit(main())
