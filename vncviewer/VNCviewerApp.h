// This file is part of UltraVNC
// https://github.com/ultravnc/UltraVNC
// https://uvnc.com/
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SPDX-FileCopyrightText: Copyright (C) 2002-2025 UltraVNC Team Members. All Rights Reserved.
// SPDX-FileCopyrightText: Copyright (C) 1999-2002 Vdacc-VNC & eSVNC Projects. All Rights Reserved.
//
 

#ifndef VNCVIEWERAPP_H__
#define VNCVIEWERAPP_H__

#pragma once

// The state of the application as a whole is contained in the app object
class VNCviewerApp;

// I doubt we'll ever want more simultaneous connections than this
#define MAX_CONNECTIONS 128

#include "ClientConnection.h"

class ClientConnection;

class VNCviewerApp {
public:
	VNCviewerApp(HINSTANCE hInstance, LPTSTR szCmdLine);

	virtual void NewConnection(bool Is_Listening) = 0;
	virtual void NewConnection(bool Is_Listening,TCHAR *host, int port) = 0;
	virtual void NewConnection(bool Is_Listening,SOCKET sock) = 0;
		
	~VNCviewerApp();

	// This should be used by Connections to register and deregister 
	// themselves. When the last connection is deregistered, the app
	// will close unless in listening mode.
	void RegisterConnection(ClientConnection *pConn);
	void DeregisterConnection(ClientConnection *pConn);
	
#ifdef KARIZMI_EXAM
    void KarizmiCloseConnection(SOCKET socket);
    void KarizmiSetControl(SOCKET socket, bool enabled);
    bool KarizmiHasConnection(SOCKET socket);
    void KarizmiShowConnection(SOCKET socket, bool fullscreen = false);
    void KarizmiThumbnail(SOCKET socket, HDC dc, int width, int height);
    ClientConnection* KarizmiFindConnection(SOCKET socket);
    SOCKET KarizmiWindowPeer(HWND window);
    bool KarizmiFocusScope(SOCKET socket,HWND window);
#endif
    VNCOptions m_options;
	HINSTANCE  m_instance;

private:
	ClientConnection *m_clilist[MAX_CONNECTIONS];
	omni_mutex m_clilistMutex;
#ifdef KARIZMI_EXAM
    DWORD karizmiUiThread=GetCurrentThreadId();
#endif
};

#endif
