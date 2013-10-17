
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_ZONE_VIEW__
#define __SOUNDPLANE_ZONE_VIEW__

#include "JuceHeader.h"
#include "SoundplaneModel.h"
#include "MLLookAndFeel.h"
#include "MLVector.h"

#ifdef _WIN32
 #include <windows.h>
#endif

#include "MLGL.h"

#ifndef GL_BGRA_EXT
 #define GL_BGRA_EXT 0x80e1
#endif

class SoundplaneZoneView  : 
	public Component,
	public OpenGLRenderer,
	public MLWidget
{
public:
    SoundplaneZoneView();
    ~SoundplaneZoneView();
 
    // when the component creates a new internal context, this is called, and
    // we'll use the opportunity to create the textures needed.
    void newOpenGLContextCreated();
    void mouseDrag (const MouseEvent& e);
    void renderOpenGL();
    void openGLContextClosing();
 	void setModel(SoundplaneModel* m);
    void resizeWidget(const MLRect& b, const int u);
	
private:
    void drawDot(Vec2 pos, float r);
    void renderGrid();
    void renderZones();

    OpenGLContext mGLContext;
	SoundplaneModel* mpModel;

};

#endif // __SOUNDPLANE_ZONE_VIEW__

