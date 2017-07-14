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
	property string currentState: "join"

	width: parent.width
	height: parent.height

	Next {
		id: nextButton
	}

	ListView {
		id: list
		model: listModel
		delegate: listDelegate
		anchors.top: nextButton.bottom
		width: parent.width
		height: parent.height - 140 * scaleFactor

		ListModel { id: listModel }
	}

	BorderImage {
		anchors.top: list.bottom
		anchors.bottom: parent.bottom
		width: parent.width
		height: 70 * scaleFactor

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

		SearchField {
			id: textInput
			anchors.left: parent.left
			anchors.right: findButton.left
			anchors.verticalCenter: parent.verticalCenter
			anchors.margins: 16 * scaleFactor
			placeholderText: "user@node"
			onAccepted: {
				var args = text.split("@")

				if (args.length === 2)
					find(args[0], args[1])
				textInput.text = ""
			}
		}

		Item {
			id: findButton
			width: 40 * scaleFactor
			height: 40 * scaleFactor
			anchors.margins: 20 * scaleFactor
			anchors.right: parent.right
			anchors.verticalCenter: parent.verticalCenter
			enabled: textInput.text.length >= 3
			Image {
				source: findMouseArea.pressed ? "icons/share_icon_pressed.png" : "icons/share_icon.png"
				width: implicitWidth * scaleFactor
				height: implicitHeight * scaleFactor
				anchors.centerIn: parent
				opacity: enabled ? 1 : 0.5
			}
			MouseArea {
				id: findMouseArea
				anchors.fill: parent
				onClicked: textInput.accepted()
			}
		}
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

			Image {
				id: removeIcon
				source: removeMouseArea.pressed ? "icons/delete_icon_pressed.png" : "icons/delete_icon.png"
				anchors.margins: 20 * scaleFactor
				anchors.verticalCenter: parent.verticalCenter
				anchors.right: parent.right
				opacity: enabled ? 1 : 0.5
				width: implicitWidth * scaleFactor
				height: implicitHeight * scaleFactor
				Behavior on opacity {NumberAnimation{duration: 100}}
				MouseArea {
					id: removeMouseArea
					anchors.fill: parent
					onClicked: listModel.remove(index)
				}
			}

			Image {
				id: addIcon
				source: addMouseArea.pressed ? "icons/add_icon_pressed.png" : "icons/add_icon.png"
				anchors.margins: 20 * scaleFactor
				anchors.verticalCenter: parent.verticalCenter
				anchors.right: removeIcon.left
				opacity: enabled ? 1 : 0.5
				width: implicitWidth * scaleFactor
				height: implicitHeight * scaleFactor
				Behavior on opacity {NumberAnimation{duration: 100}}

				MouseArea {
					id: addMouseArea
					anchors.fill: parent
					onClicked: {
						if (currentState === "join") {
							join(getName(name));
						} else if (currentState === "accept") {
							var args = JSON.parse(name);

							accept(args.user, args.node, args.name);
						}
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

		return getDigest(args.user) + "@" + args.node;
	}

	function getName(s) {
		var args = JSON.parse(s);

		return args.name;
	}

	function showResult(state, result) {
		currentState = state;
		statusText.visible = false;
		listModel.clear();
		listModel.append({"name": JSON.stringify(result)})
	}

	function clearResult(result) {
		for (var i = 0; i < listModel.count; i++)
			if (getName(listModel.get(i).name) == result)
				listModel.remove(i);
	}

	function find(user, node) {
		sock.active = false;
		request = JSON.stringify({'op':'find', 'user':user, 'node':node});
		sock.active = true;
	}

	function onFind(result) {
		showResult("join", result);
	}

	function wait() {
		if (listModel.count === 0) {
			statusText.text = "Join ...";
			statusText.visible = true;
		}

		sock.active = false;
		request = JSON.stringify({'op':'wait'});
		sock.active = true;
	}

	function onWait(result) {
		showResult("accept", result);
	}

	function join(name) {
		sock.active = false;
		request = JSON.stringify({'op':'join', 'name':name});
		sock.active = true;
	}

	function onJoin(result) {
		clearResult(result);
	}

	function accept(user, node, name) {
		sock.active = false;
		request = JSON.stringify({'op':'accept', 'user':user, 'node':node, 'name':name});
		sock.active = true;
	}

	function onAccept(result) {
		clearResult(result);
	}

	function checkMessage(message) {
		var args = JSON.parse(message);
		var result = args.result;
		var op = args.op;

		if (JSON.stringify(result) == '{}')
			return;

		if (op == 'find')
			onFind(result);
		else if (op == 'wait')
			onWait(result);
		else if (op == 'join')
			onJoin(result);
		else if (op == 'accept')
			onAccept(result);
	}
}
