// Direct camera control for the mouse (Windows) and touch drags (Android): while one turns the camera, the game's
// camera object gets no stick threshold and no acceleration, so the camera follows the stick linearly; its own values
// come back when it stops, so controllers behave as on the console.
#pragma once

namespace camera_assist {

// every pad poll: active while the mouse or a drag turns the camera; speed multiplies a fully ramped stick's speed
void Update(bool active, float speed);

}  // namespace camera_assist
