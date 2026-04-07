#!/usr/bin/python3

import socket
import json
import argparse
import sys
import os
import collections
import pandas

from PyQt5 import QtCore, uic
from PyQt5.QtCore import QSize, Qt
from PyQt5.QtWidgets import (
    QApplication, QWidget, QMainWindow,
    QLabel, QMenu, QToolBar, QAction, QStatusBar, QMessageBox,
    QPushButton, QTableView, QLineEdit,
    QVBoxLayout, QHBoxLayout, QAbstractItemView,
    QPlainTextEdit, QSplitter
)
from PyQt5.QtGui import (
    QIcon, QKeySequence, QTextCursor
)

from PyQt5.QtCore import QTimer, QProcess, QProcessEnvironment, QSortFilterProxyModel, pyqtSignal, QModelIndex
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

def setup_filterentry(entry, listview, model):
    filtermodel = QSortFilterProxyModel()
    filtermodel.setSourceModel(model)
    filtermodel.setFilterKeyColumn(-1)
    entry.textChanged.connect(filtermodel.setFilterFixedString)
    listview.setModel(filtermodel)
    return filtermodel

_BASEDIR = os.path.abspath(os.path.dirname(__file__))
def loadUi(filename, parent):
    uic.loadUi(os.path.join(_BASEDIR, filename), parent)

class MainWindow(QMainWindow):
    def __init__(self, client, process):
        super().__init__()
        self.client = client
        self.process = process

        loadUi("MainWindow.ui", self)

        self.pause_icon = QIcon.fromTheme('media-playback-pause')
        self.listmodel = PandasModel(pandas.DataFrame(columns=['id', 'function', 'file', 'line', 'flags', 'tripcount']),
                                     columns=['function', 'file', 'line'],
                                     decoration=lambda q,col: self.pause_icon if q.flags and col == 0 else None,
                                     name='Query')
        self.filtermodel = setup_filterentry(self.entry, self.itemview, self.listmodel)
        self.client.request('loadQueries', response=self.listmodel.populate)
        self.itemview.doubleClicked.connect(self.show_query)
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
            next_query = self.listmodel.get(idx.row())

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
            self.listmodel._dataframe.at[qid, 'flags'] = response['flags']
            self.listmodel.layoutChanged.emit()

        self.client.request('set_break',
                            response=flagsChanged,
                            qid=query.id,
                            flags=int(break_here))

    def quit_app(self):
        QApplication.exit(0)

def read_dataframe(js_reply):
    count = collections.Counter()
    columns = []
    for c in js_reply['columns']:
        if c in count:
            columns.append("{}.{}".format(c, count[c]))
        else:
            columns.append(c)
        count.update([c])
    return pandas.DataFrame(columns=columns, data=js_reply['data'])

# Taken from QT's example code in:
# https://doc.qt.io/qtforpython-6/examples/example_external_pandas.html
class PandasModel(QtCore.QAbstractTableModel):
    """A model to interface a Qt view with pandas dataframe """

    def __init__(self, dataframe: pandas.DataFrame, parent=None, *, name:str, columns=[], decoration=None, vertical=False, allow_get=False):
        super().__init__(parent)
        self._dataframe = dataframe
        assert(all(col in dataframe.columns for col in columns))
        self._columns = columns if columns else list(dataframe.columns)
        if decoration is not None or allow_get:
            self.classview = collections.namedtuple(name, dataframe.columns)
        self.decoration = decoration
        self.vertical = vertical

    def get(self, row):
        return self.classview(*self._dataframe.iloc[row])

    def populate(self, result):
        newframe = read_dataframe(result)
        assert(self._dataframe.columns.equals(newframe.columns))
        self._dataframe = pandas.concat([self._dataframe, newframe])
        if len(newframe):
            self.layoutChanged.emit()

    def rowCount(self, parent=QModelIndex()) -> int:
        """ Override method from QAbstractTableModel

        Return row count of the pandas DataFrame
        """
        if parent == QModelIndex():
            return len(self._dataframe)

        return 0

    def columnCount(self, parent=QModelIndex()) -> int:
        """Override method from QAbstractTableModel

        Return column count of the pandas DataFrame
        """
        if parent == QModelIndex():
            return len(self._columns)
        return 0

    def data(self, index: QModelIndex, role=Qt.ItemDataRole):
        """Override method from QAbstractTableModel

        Return data cell from the pandas DataFrame
        """
        if not index.isValid():
            return None

        col = self._columns[index.column()]
        if role == Qt.DisplayRole:
            return str(self._dataframe[col].iloc[index.row()])

        elif role == Qt.DecorationRole and self.decoration is not None:
            return self.decoration(self.get(index.row()), index.column())

        return None

    def headerData(
            self, section: int, orientation: Qt.Orientation, role: Qt.ItemDataRole
    ):
        """Override method from QAbstractTableModel

        Return dataframe index as vertical header data and columns as horizontal header data.
        """
        if role == Qt.DisplayRole:
            if orientation == Qt.Horizontal:
                return str(self._columns[section])

            elif orientation == Qt.Vertical and self.vertical:
                return str(self._dataframe.index[section])

        return None

class BreakWindow(QMainWindow):
    def __init__(self, client):
        super().__init__()
        self.client = client
        loadUi("BreakWindow.ui", self)

        self._open_tabs = {}
        self.tabWidget.clear()
        self.tabWidget.tabCloseRequested.connect(self._close_tab)
        self.client.request('show_query', response=self.set_query)

    def set_query(self, q):
        self.query = q
        tables = read_dataframe(q['db'])
        self.dbmodel = PandasModel(tables, name='Table', allow_get=True)
        self.filtermodel = setup_filterentry(self.entry, self.itemview, self.dbmodel)
        self.itemview.doubleClicked.connect(self._on_table_double_clicked)

    def _on_table_double_clicked(self, index):
        src_index = self.filtermodel.mapToSource(index)
        name = self.dbmodel.get(src_index.row()).name
        if name in self._open_tabs:
            self.tabWidget.setCurrentWidget(self._open_tabs[name])
        else:
            self.client.request('get_table', src_index.row(), response=lambda result: self.add_table(name, result))

    def add_table(self, name, result):
        df = read_dataframe(result)
        model = PandasModel(df, name=name, vertical=True)
        view = QTableView()
        view.setModel(model)
        self._open_tabs[name] = view
        self.tabWidget.addTab(view, name)
        self.tabWidget.setCurrentWidget(view)

    def _close_tab(self, index):
        widget = self.tabWidget.widget(index)
        name = next((k for k, v in self._open_tabs.items() if v is widget), None)
        if name:
            del self._open_tabs[name]
        self.tabWidget.removeTab(index)

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

    def breakpoint(self, params):
        self.breakwindow = BreakWindow(self.client)
        self.breakwindow.show()

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
        self.client.register_method('breakpoint', self.breakpoint)

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
