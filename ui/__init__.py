import os
import sys
import platform
import subprocess
from PyQt5 import QtCore
from PyQt5.QtCore import QObject, QUrl
from PyQt5.QtQml import QQmlApplicationEngine
from PyQt5.QtGui import QGuiApplication, QFont
from defaults import BACKEND_ADDR, BACKEND_PORT

def _set_addr(root):
	component = root.findChild(QObject, "vhubAddr")
	if component:
		component.setProperty("text", "ws://%s:%d" % (BACKEND_ADDR, BACKEND_PORT))

def start(dirname):
    font = QFont()
    font.setFamily("Helvetica")
    app = QGuiApplication(sys.argv)
    app.setFont(font)
    engine = QQmlApplicationEngine()
    engine.load(QUrl.fromLocalFile(os.path.join(dirname, 'ui', 'qml','UI.qml')))
    objs = engine.rootObjects()
    if objs:
        root = objs[0]
        _set_addr(root)
        root.show()
        app.exec_()
