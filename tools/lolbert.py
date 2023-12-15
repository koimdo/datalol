#!/usr/bin/python3

import socket
import json
import argparse
import sys
import os

from PyQt5 import QtCore, uic
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

from PyQt5.QtCore import QTimer, QProcess, QProcessEnvironment, QSortFilterProxyModel, pyqtSignal
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
        self.methods = {}

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
            if 'method' in response:
                handler = self.methods[response['method']]
                params = response['params']
                reqid = response.get('id', None)
                print("Running method {}({})".format(handler, params))
                res = handler(params) # FIXME: unpack?
                if reqid is None:
                    # a notification
                    assert(res is None)
                else:
                    self._write({'id': reqid,
                                 'result': res})
            elif 'result' in response:
                handler = self.in_progress.pop(response['id'])
                handler(response['result'])
            else:
                err = response['error']
                raise Error(err['code'], err['message'], err.get('data', None))

    def finished(self):
        print("Channel closed")

    def register_method(self, method, handler):
        self.methods[method] = handler

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

class Query:
    def __init__(self, file, id, function, line, flags, tripcount):
        self.file = file
        self.id = id
        self.function = function
        self.line = line
        self.flags = flags
        self.tripcount = tripcount

class QueriesModel(QtCore.QAbstractListModel):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.items = []
        self.pause_icon = QIcon.fromTheme('media-playback-pause')

    def populate(self, items):
        self.items = [Query(**d) for d in items]
        self.layoutChanged.emit()

    def data(self, index, role):
        item = self.items[index.row()]
        if role == Qt.DisplayRole:
            return "{0.file}:{0.line} {0.function}".format(item)
        elif role == Qt.DecorationRole:
            if item.flags:
                return self.pause_icon

    def rowCount(self, index):
        return len(self.items)

class MainWindow(QMainWindow):
    def __init__(self, client, process):
        super().__init__()
        self.client = client
        self.process = process

        uic.loadUi("MainWindow.ui", self)

        self.listmodel = QueriesModel()
        self.filtermodel = QSortFilterProxyModel()
        self.filtermodel.setSourceModel(self.listmodel)
        self.entry.textChanged.connect(self.filtermodel.setFilterFixedString)
        self.client.request('loadQueries', response=self.listmodel.populate)
        self.itemview.doubleClicked.connect(self.show_query)
        self.itemview.setModel(self.filtermodel)
        self.itemview.selectionModel().selectionChanged.connect(self.selectionChanged)

        self.itemview.addAction(self.actionBreakpoint)
        self.itemview.addAction(self.actionSource)
        self.actionSource.triggered.connect(self.show_query)
        self.actionBreakpoint.triggered.connect(self.set_breakpoint)

        self.actionResume.triggered.connect(lambda: self.client.request('resume'))
        self.actionQuit.triggered.connect(self.quit_app)

        self.queryChanged.connect(self.handleQChanged)

        self.current_query = None

    queryChanged = pyqtSignal()
    def selectionChanged(self, new, old):
        if not new:
            next_query = None
        else:
            assert(len(new) == 1)
            idx = new.indexes()[0]
            idx = self.filtermodel.mapToSource(idx)
            next_query = self.listmodel.items[idx.row()]

        if self.current_query is not next_query:
            self.current_query = next_query
            self.queryChanged.emit()

    def handleQChanged(self):
        q = self.current_query
        self.actionSource.setEnabled(q is not None)
        self.actionBreakpoint.setEnabled(q is not None)
        self.actionBreakpoint.setChecked(q is not None and q.flags)

    def show_query(self, idx=None):
        query = self.current_query
        text = None
        with open(query.file, 'r') as f:
            text = f.read()
        assert(text is not None)
        self.rhs.setPlainText(text)
        cursor = QTextCursor(self.rhs.document().findBlockByLineNumber(query.line))
        self.rhs.setTextCursor(cursor)
        cursor.select(QTextCursor.LineUnderCursor);

    def set_breakpoint(self, break_here):
        query = self.current_query
        def flagsChanged(response):
            qid = response['qid']
            self.listmodel.layoutAboutToBeChanged.emit()
            self.listmodel.items[qid].flags=response['flags']
            self.listmodel.layoutChanged.emit()

        self.client.request('set_break',
                            response=flagsChanged,
                            qid=query.id,
                            flags=int(break_here))

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
        self.window = MainWindow(self.client, self.process)
        self.window.show()

    def _runprog(self):
        (mysocket, sub_fd) = socket.socketpair()
        sub_env = QProcessEnvironment.systemEnvironment()
        sub_env.insert('LOLBERT_FD', str(sub_fd.fileno()))
        sub_fd.set_inheritable(True)
        self.client = Client(mysocket)
        process = QProcess()
        process.setInputChannelMode(QProcess.ForwardedInputChannel)
        process.setProcessChannelMode(QProcess.ForwardedChannels)
        process.setProcessEnvironment(sub_env)
        process.start(self.args.progargs[0], self.args.progargs[1:])
        self.process = process

    def start(self):
        self._runprog()
        self.client.register_method('hello', self.hello)

def main():
    parser = argparse.ArgumentParser(prog='interp test',
                    description='Test for simple interpreter')
    parser.add_argument('--verbose', dest='verbose', action='store_const',
                        const=True, default=False,
                        help='debug output')
    parser.add_argument('--args', dest='progargs', nargs=argparse.REMAINDER, help="Program and arguments to run")
    args = parser.parse_args()

    signal.signal(signal.SIGINT, sigint_handler)
    app = LolbertApp(args)
    return app.exec()

if __name__ == '__main__':
    sys.exit(main())
