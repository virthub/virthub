/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtEnginio module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Digia Plc and its Subsidiary(-ies) nor the names
**     of its contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

import QtQuick 2.3
import Qt.WebSockets 1.0
import QtQuick.Layouts 1.0
import QtQuick.Controls 1.0 as Controls

Rectangle {
  width: 400
  height: 600
  property var request: ""

  Header {
    id: header
  }

  BorderImage {
    id: input
    width: parent.width
    anchors.top: header.bottom
    anchors.bottom: parent.bottom
    source: "images/delegate.png"
    border.left: 5; border.top: 5
    border.right: 5; border.bottom: 5

    Rectangle {
      y: -1 ; height: 1
      width: parent.width
      color: "#bbb"
    }

    Rectangle {
      y: 0 ; height: 1
      width: parent.width
      color: "white"
    }
  }

  Column {
    anchors.centerIn: parent
    anchors.alignWhenCentered: true
    width: 360 * scaleFactor
    spacing: 14 * intScaleFactor

    TextField {
      id: nameInput
      onAccepted: passwordInput.forceActiveFocus()
      placeholderText: "Username"
      KeyNavigation.tab: passwordInput
    }

    TextField {
      id: passwordInput
      onAccepted: login()
      placeholderText: "Password"
      echoMode: TextInput.Password
      KeyNavigation.tab: loginButton
    }

    Row {
      spacing: 20 * scaleFactor
      width: 360 * scaleFactor
      anchors.horizontalCenter: parent.horizontalCenter
      anchors.alignWhenCentered: true

      TouchButton {
        id: loginButton
        text: "Login"
        baseColor: "#2af"
        width: (parent.width - parent.spacing)/2
        onClicked: login()
        enabled: nameInput.text.length && passwordInput.text.length >= 3
        KeyNavigation.tab: registerButton
      }
      
      TouchButton {
        id: registerButton
        text: "Register"
        onClicked: register()
        width: (parent.width - parent.spacing)/2
        enabled: nameInput.text.length && passwordInput.text.length >= 3
        KeyNavigation.tab: nameInput
      }
    }
  }

  Text {
    id: statusText
    anchors.bottom: parent.bottom
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottomMargin: 70 * scaleFactor
    font.pixelSize: 18 * scaleFactor
    color: "#999"
  }

  Controls.Stack.onStatusChanged: {
    if (Controls.Stack.status == Controls.Stack.Activating) {
      nameInput.text = "";
      statusText.text = "";
      passwordInput.text = "";
      nameInput.forceActiveFocus();
    }
  }

  WebSocket {
    id: sock
    url: vhubAddr.text
    onTextMessageReceived: {
      var args = JSON.parse(message);

      if (args.op == 'login')
        onLogin(args.result);
    }
    onStatusChanged: {
      if (sock.status == WebSocket.Error) {
        updateStatus(sock.errorString);
      } else if (sock.status == WebSocket.Open) {
        if (request != "")
          sock.sendTextMessage(request);
      }
    }
    active: true
  }

  function onLogin(result) {
    vhubNode.text = result.node;
    main.title = vhubNode.text;
    finder.wait();
    members.wait();
    mainView.push({ item: finder });
  }

  function login() {
    if (sock.active)
      sock.active = false;
    request = JSON.stringify({'op':'login', 'user':nameInput.text, 'password':passwordInput.text});
    sock.active = true;
  }

  function register() {
  }

  function updateStatus(s) {
    statusText.text = s;
  }
}
