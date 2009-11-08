// Fork from the Tongseng project (Fajran Iman Rusadi <fajran@gmail.com>)
// for using it with the MultiTouchPad class of SuperCollider.
// Copyright (C) Batuhan Bozkurt <batuhan@batuhanbozkurt.com>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

// Based on multitouch code from http://www.steike.com/code/multitouch/

#include "tongsengmod.h"
#include "mt.h"
#include <iostream>
#include <math.h>
#include <unistd.h>

#include <TuioServer.h>
#include <TuioObject.h>
#include <TuioTime.h>

#include <set>
#include <map>

#define D(s) /* std::cout << s << std::endl; */

#define EXISTS(c, v) ((c).find(v) != (c).end())
#define FOREACH(c, i) for (i=(c).begin(); i!=(c).end(); i++)

static MTDeviceRef dev;
static TUIO::TuioServer* server;
static TUIO::OscSender* oscSender;
static bool running = false;
static bool verbose = false;
static std::string host("localhost");
static int port = 3333;

// Sensitivity
#define MIN_DISTANCE 0.00001f

// Events dispatch interval
#define SAMPLING_INTERVAL 20000
static long lastFrameTimeSec = -1;
static long lastFrameTimeUsec = -1;

// Active fingers
static std::set<int> currentFingers;

// Position of active fingers
static std::map<int, float> lastX;
static std::map<int, float> lastY;

// Finger to TUIO cursors mapping
static std::map<int, TUIO::TuioObject*> cursors;

// Check if a position changes is considered as a movement
static inline bool isMoving(int id, float x, float y)
{
	float dx = x - lastX[id];
	float dy = y - lastY[id];
	float distance2 = dx*dx + dy*dy;
	D("distance: id=" << id << ", distance=" << distance2);
	return distance2 > MIN_DISTANCE;
}

// New touch
static void touch_add(int id, float x, float y, float size)
{
	D("touch_add: id=" << id << ", x=" << x << ", y=" << y);
	lastX[id] = x;
	lastY[id] = y;

	TUIO::TuioObject* cursor = server->addTuioObject(id, x, y, size);
	cursors[id] = cursor;
}

// Update touch
static void touch_update(int id, float x, float y, float size)
{
	if (isMoving(id, x, y)) {
		D("touch_update: id=" << id << ", x=" << x << ", y=" << y);
		lastX[id] = x;
		lastY[id] = y;

		TUIO::TuioObject* cursor = cursors[id];
		server->updateTuioObject(cursor, x, y, size);
	}
}

// Remove touch
static void touch_remove(int id)
{
	lastX.erase(id);
	lastY.erase(id);

	D("touch_remove: id=" << id);

	TUIO::TuioObject* cursor = cursors[id];
	server->removeTuioObject(cursor);
	cursors.erase(id);
}

// Event dispatch guard
static bool sampling_interval_passed()
{
	TUIO::TuioTime currentTime = TUIO::TuioTime::getSessionTime();
	long dsec = currentTime.getSeconds() - lastFrameTimeSec;
	long dusec = currentTime.getMicroseconds() - lastFrameTimeUsec;

	bool res = false;

	if (lastFrameTimeSec == -1) {
		lastFrameTimeSec = currentTime.getSeconds();
		lastFrameTimeUsec = currentTime.getMicroseconds();
		res = true;
	}
	else if (dsec > 1) {
		res = true;
	}
	else {
		long delta =  dsec * 1000000 + dusec;
		if (delta > SAMPLING_INTERVAL) {
			res = true;
		}
	}

	return res;
}

// Start creating TUIO messages
static void tuio_frame_begin()
{
	TUIO::TuioTime currentTime = TUIO::TuioTime::getSessionTime();
	server->initFrame(currentTime);
}

// Flush TUIO messages
static void tuio_frame_end()
{
	server->stopUntouchedMovingObjects();
	server->commitFrame();
}

// Process incoming events
static int callback(int device, Finger *data, int nFingers, double timestamp, int frame)
{
	if (!running || !sampling_interval_passed()) {
		return 0;
	}

	tuio_frame_begin();

	std::set<int> fingers;

	// Process incoming events
	int i;
	for (i=0; i<nFingers; i++) {
		Finger *f = &data[i];
		int id = f->identifier;

		float x = f->normalized.pos.x;
		float y = 1.0f - f->normalized.pos.y; // reverse y axis
		float size = f->size;
		
		if (EXISTS(currentFingers, id)) {
			// update
			touch_update(id, x, y, size);
		}
		else {
			// add
			touch_add(id, x, y, size);
		}
		fingers.insert(id);
	}

	// Remove old events
	std::set<int>::iterator iter;
	FOREACH(currentFingers, iter) {
		int id = *iter;
		if (!EXISTS(fingers, id)) {
			// remove
			touch_remove(id);
		}
	}

	// Update active fingers list
	currentFingers.clear();
	currentFingers.insert(fingers.begin(), fingers.end());

	tuio_frame_end();

	return 0;
}

// Start TUIO server
static void tuio_start()
{
	oscSender = new TUIO::UdpSender((char*)host.c_str(), port);
	server = new TUIO::TuioServer(oscSender);
	server->setVerbose(verbose);
	server->setSourceName("Tongsengmod");
	server->enableObjectProfile(true);
	server->enableBlobProfile(false);
	server->enableCursorProfile(false);
}

// Release all active fingers
static void release_all_fingers()
{
	tuio_frame_begin();

	std::set<int>::iterator iter;
	FOREACH(currentFingers, iter) {
		touch_remove(*iter);
	}

	tuio_frame_end();
}

// Stop TUIO server
static void tuio_stop()
{
	release_all_fingers();
	delete server;
	delete oscSender;
}

// Start handling multitouch events
static void mt_start()
{
	dev = MTDeviceCreateDefault();
	MTRegisterContactFrameCallback(dev, callback);
	MTDeviceStart(dev, 0);
}

// Stop handling multitouch events
static void mt_stop()
{
	MTUnregisterContactFrameCallback(dev, callback);
	MTDeviceStop(dev);
	MTDeviceRelease(dev);
}

// Set hostname and port that will be used by TUIO server
void tongseng_set_hostname_and_port(const char* _hostname, int _port)
{
	host = _hostname;
	port = _port;
}

// Set TUIO server verbosity
void tongseng_set_verbose(int _verbose)
{
	verbose = _verbose != 0;
}

// Start Tongseng
void tongseng_start()
{
	mt_start();
	tuio_start();
	running = true;
}

// Stop Tongseng
void tongseng_stop()
{
	if (running) {
		running = false;
		tuio_stop();
		mt_stop();
	}
}

