"use strict";

// Register functions reachable via Blue.callIsland() from the AOT core.
var island = globalThis.__BLUE_ISLAND || (globalThis.__BLUE_ISLAND = {});

var pingCount = 0;

island.handlePing = function(payload) {
  pingCount++;
  var data;
  try { data = JSON.parse(payload || "{}"); } catch (e) { data = {}; }
  console.log("[Island] handlePing #" + pingCount + ":", data.msg || "(no msg)");
  return JSON.stringify({
    reply: "Hello from the Island!",
    received: data.msg || "",
    pingCount: pingCount,
    islandTime: Date.now()
  });
};

console.log("[Island] Island ready.");
