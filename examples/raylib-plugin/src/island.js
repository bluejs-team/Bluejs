"use strict";

if (!Blue.Plugin.load("examples/raylib-plugin/build/blue_raylib.so")) {
  throw new Error("failed to load raylib plugin. Run: make -C examples/raylib-plugin");
}

Raylib.initWindow(800, 450, "Blue raylib plugin");

var frames = 0;
while (!Raylib.windowShouldClose() && frames < 600) {
  Raylib.beginDrawing();
  Raylib.clearBackground(24, 26, 32);
  Raylib.drawText("Blue native C plugin + raylib", 80, 170, 28, 120, 210, 255);
  Raylib.drawText("Close the window to exit", 80, 215, 20, 230, 230, 230);
  Raylib.endDrawing();
  frames++;
}

Raylib.closeWindow();
