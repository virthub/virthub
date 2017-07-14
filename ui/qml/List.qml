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

Rectangle {
  property string request: ""

  width: parent.width
  height: parent.height

  Back {
    id: backButton
  }

  ListView {
    id: list
    model: listModel
    delegate: listDelegate
    anchors.top: backButton.bottom
    width: parent.width
    height: parent.height - 140 * scaleFactor

    ListModel { id: listModel }
  }

  Component {
    id: listDelegate
    BorderImage {
      id: item
      width: parent.width ; height: 70 * scaleFactor
      source: mouse.pressed ? "images/delegate_pressed.png" : "images/delegate.png"
      border.left: 5; border.top: 5
      border.right: 5; border.bottom: 5

      Image {
        id: shadow
        anchors.top: parent.bottom
        width: parent.width
        visible: !mouse.pressed
        source: "images/shadow.png"
      }

      MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
      }

      Text {
        id: listItem
        text: getText(name)
        font.pixelSize: 26 * scaleFactor
        color: "#333"

        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 16 * scaleFactor
        anchors.rightMargin: 60 * scaleFactor
        elide: Text.ElideRight
      }

      // Show a delete button when the mouse is over the delegate
      Image {
        id: removeIcon
        source: removeMouseArea.pressed ? "icons/delete_icon_pressed.png" : "icons/delete_icon.png"
        anchors.margins: 20 * scaleFactor
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        opacity: enabled ? 1 : 0.5
        width:  implicitWidth * scaleFactor
        height: implicitHeight * scaleFactor
        Behavior on opacity {NumberAnimation{duration: 100}}
        MouseArea {
          id: removeMouseArea
          anchors.fill: parent
          onClicked: {
            var state = getState(name);

            if (state === "accept")
              drop(getName(name));
            else if (state === "join")
              remove(getName(name));
          }
        }
      }
    }
  }

  WebSocket {
    id: sock
    url: vhubAddr.text
    onTextMessageReceived: {
      checkMessage(message);
      wait();
    }
    onStatusChanged: {
      if (sock.status == WebSocket.Open) {
        if (request != "")
          sock.sendTextMessage(request);
      }
    }
    active: true
  }

  Text {
    id: statusText
    anchors.verticalCenter: parent.verticalCenter
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottomMargin: 70 * scaleFactor
    font.pixelSize: 20 * scaleFactor
    color: "#999"
  }

  function getDigest(s) {
    if (s.length > 4)
      return s.substring(0, 4) + "*";
    else
      return s;
  }

  function getText(s) {
    var args = JSON.parse(s);

  	if (args.state == 'accept')
    	return	" >> " + getDigest(args.user) + "@" + args.node;
  	else
  		return	" << " + getDigest(args.user) + "@" + args.node;
  }

  function getName(s) {
    var args = JSON.parse(s);

    return args.name;
  }

  function getState(s) {
    var args = JSON.parse(s);

    return args.state;
  }

  function checkMessage(message) {
  	var args = JSON.parse(message);
    var result = args.result;
    var op = args.op;

    if (JSON.stringify(result) == '{}')
    	return;

    if (op == 'list')
      onList(result);
    else if (op == 'drop')
      onDrop(result);
    else if (op == 'remove')
      onRemove(result);
  }

  function wait() {
    if (listModel.count == 0) {
      statusText.text = "No device";
      statusText.visible = true;
    }
    sock.active = false;
    request = JSON.stringify({'op':'list'});
    sock.active = true;
  }

  function onList(result) {
    var memberList = result.split(";");

    if (memberList.length > 0) {
      statusText.visible = false;
      listModel.clear();
      for (var i = 0; i < memberList.length; i++)
        listModel.append({"name": memberList[i]});
    }
  }

  function clearResult(result) {
    for (var i = 0; i < listModel.count; i++)
      if (getName(listModel.get(i).name) == result)
        listModel.remove(i);
  }

  function drop(name) {
    sock.active = false;
    request = JSON.stringify({'op':'drop', 'name':name});
    sock.active = true;
  }

  function onDrop(result) {
  	clearResult(result);
  }

  function remove(name) {
    sock.active = false;
    request = JSON.stringify({'op':'remove','name':name});
    sock.active = true;
  }

  function onRemove(result) {
	  clearResult(result);
  }
}
