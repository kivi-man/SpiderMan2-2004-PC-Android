package com.spiderman2.xbox;

import org.libsdl.app.SDLActivity;

// SDL is linked into libmain.so with the game: one library to load
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"main"};
    }
}
