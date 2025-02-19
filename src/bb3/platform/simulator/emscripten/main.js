////////////////////////////////////////////////////////////////////////////////
var headerElement = document.getElementById("header");
var outputElement = document.getElementById("output");
var footerElement = document.getElementById("footer");
var canvasContainerElement = document.getElementById("canvas-container");
var canvasElement = document.getElementById("canvas");
const urlParams = new URLSearchParams(window.location.search);
const simulatorID = urlParams.get("id");
let resizeCanvas;
let scpiInputBuffer = [];
let debuggerInputBuffer = [];
if (!simulatorID) {
  headerElement.style.display = "flex";
  outputElement.style.display = "block";
  footerElement.style.display = "flex";
} else {
  canvasContainerElement.style.marginTop = "0";
  canvasContainerElement.style.height = "100%";
  canvasContainerElement.style.display = "flex";
  canvasContainerElement.style.alignItems = "center";
  canvasContainerElement.style.justifyContent = "center";
  canvasElement.style.maxWidth = "100vw";
  canvasElement.style.maxHeight = "100vh";
  canvasElement.style.objectFit = "contain";

  window.onmessage = function (e) {
    if (e.data.msgId == "web-simulator-connection-scpi-write") {
      scpiInputBuffer.push(e.data.data);
    } else if (e.data.msgId == "web-simulator-connection-debugger-write") {
      debuggerInputBuffer.push(e.data.data);
    } else if (e.data.msgId == "web-simulator-connection-debugger-client-connected") {
      Module.onDebuggerClientConnected();
    } else if (e.data.msgId == "web-simulator-connection-debugger-client-disconnected") {
      Module.onDebuggerClientDisconnected();
    }
  };

  window.top.postMessage(
    {
      msgId: "web-simulator-loaded",
      simulatorID,
    },
    "*"
  );
}

function arrayBufferToArrayBufferWithSize(arrayBuffer) {
  var view = new Uint8Array(arrayBuffer);
  const length = view.length;
  const result = new Uint8Array(4 + length);

  result[0] = (length & 0xff000000) >> 24;
  result[1] = (length & 0xff0000) >> 16;
  result[2] = (length & 0xff00) >> 8;
  result[3] = length & 0xff;

  result.set(view, 4);

  return result;
}

function readScpiInputBuffer() {
  if (scpiInputBuffer.length == 0) {
    return null;
  }

  return arrayBufferToArrayBufferWithSize(scpiInputBuffer.shift());
}

function writeScpiOutputBuffer(arr) {
  const scpiOutputBuffer = (new Uint8Array(arr)).buffer;

  window.top.postMessage(
    {
      msgId: "web-simulator-write-scpi-buffer",
      simulatorID,
      scpiOutputBuffer
    },
    "*"
  );
}

function readDebuggerInputBuffer() {
  if (debuggerInputBuffer.length == 0) {
    return null;
  }

  return arrayBufferToArrayBufferWithSize(debuggerInputBuffer.shift());
}

function writeDebuggerOutputBuffer(arr) {
  const debuggerOutputBuffer = (new Uint8Array(arr)).buffer;

  window.top.postMessage(
    {
      msgId: "web-simulator-write-debugger-buffer",
      simulatorID,
      debuggerOutputBuffer
    },
    "*"
  );
}

////////////////////////////////////////////////////////////////////////////////

var statusElement = document.getElementById("status");
var progressElement = document.getElementById("progress");
var spinnerElement = document.getElementById("spinner");

var stdinBuffer = [];

var terminal = $("#output").terminal(
  function (command) {
    // send command characters one by one, with 10ms interval, otherwise it will block for unknown reason
    const sendChar = () => {
      if (i < command.length) {
        stdinBuffer.push(command.charCodeAt(i));
        i++;
        setTimeout(sendChar, 10); // 10ms
      } else {
        stdinBuffer.push(13);
      }
    };
    let i = 0;
    sendChar();
  },
  {
    greetings: "",
    name: "js_demo",
    prompt: "[[;yellow;]scpi> ]",
  }
);

var Module = {
  preRun: [
    function () {
      var lastCh = null;

      function stdin() {
        var ch = stdinBuffer.shift();
        if (ch !== undefined) {
          lastCh = ch;
        } else {
          // if no input then send 0, null, 0, null, ... to trick emscripten,
          // otherwise it will stop calling this function
          lastCh = lastCh === null ? 0 : null;
        }
        return lastCh;
      }

      FS.init(stdin);
    },
  ],

  postRun: [],

  print: function (text) {
    if (arguments.length > 1) {
      text = Array.prototype.slice.call(arguments).join(" ");
    }

    console.log(text);

    if (text.startsWith("**ERROR")) {
      terminal.error(text);
    } else {
      terminal.echo(text);
    }
  },

  printErr: function (text) {
    if (arguments.length > 1) {
      text = Array.prototype.slice.call(arguments).join(" ");
    }
    console.error(text);
  },

  setStatus: function (text) {
    if (!Module.setStatus.last)
      Module.setStatus.last = { time: Date.now(), text: "" };
    if (text === Module.setStatus.last.text) return;
    var m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
    var now = Date.now();
    if (m && now - Module.setStatus.last.time < 30) return; // if this is a progress update, skip it if too soon
    Module.setStatus.last.time = now;
    Module.setStatus.last.text = text;
    if (m) {
      text = m[1];
      progressElement.value = parseInt(m[2]) * 100;
      progressElement.max = parseInt(m[4]) * 100;
      progressElement.hidden = false;
      spinnerElement.hidden = false;
    } else {
      progressElement.value = null;
      progressElement.max = null;
      progressElement.hidden = true;
      if (!text) spinnerElement.style.display = "none";
    }
    statusElement.innerHTML = text;
  },

  totalDependencies: 0,

  monitorRunDependencies: function (left) {
    this.totalDependencies = Math.max(this.totalDependencies, left);
    Module.setStatus(
      left
        ? "Preparing... (" +
            (this.totalDependencies - left) +
            "/" +
            this.totalDependencies +
            ")"
        : "All downloads complete."
    );
  },

  onRuntimeInitialized: function () {
    canvasElement.width = 1396;
    canvasElement.height = 563;
    var ctx = canvas.getContext('2d');

    var wheelDeltaY = 0
    var wheelClicked = 0;

    function update() {
      Module._onMouseWheelEvent(wheelDeltaY, wheelClicked);
      wheelDeltaY = 0;
      wheelClicked = 0;

      Module._mainLoop();

      var buf_addr = Module._getSyncedBuffer();

      if (buf_addr != 0) {
        var uint8ClampedArray = new Uint8ClampedArray(Module.HEAPU8.subarray(buf_addr, buf_addr + canvasElement.width * canvasElement.height * 4));
        var imgData = new ImageData(uint8ClampedArray, canvasElement.width, canvasElement.height);

        ctx.putImageData(imgData, 0, 0);
      }
    
      window.requestAnimationFrame(update);
    }

    window.requestAnimationFrame(update);

    function sendPointerEvent(event) {
      var bbox = canvasElement.getBoundingClientRect();
      Module._onPointerEvent(
        (event.clientX - bbox.left) * (canvasElement.width / bbox.width),
        (event.clientY - bbox.top) * (canvasElement.height / bbox.height),
        event.buttons == 1 ? 1 : 0
      );
      event.preventDefault();
      event.stopPropagation();
    }

    canvas.addEventListener("pointerdown", event => {
      if (event.buttons == 4) {
        wheelClicked = 1;
      }
      canvasElement.setPointerCapture(event.pointerId);
      sendPointerEvent(event);
    }, true);    

    canvas.addEventListener("pointermove", event => {
      sendPointerEvent(event);
    }, true);    

    canvas.addEventListener("pointerup", event => {
      canvasElement.releasePointerCapture(event.pointerId);
      sendPointerEvent(event);
    }, true);    

    canvas.addEventListener("pointercancel", event => {
      canvasElement.releasePointerCapture(event.pointerId);
      sendPointerEvent(event);
    }, true);       

    document.addEventListener("wheel", event => {
      wheelDeltaY += -event.deltaY;
    }, true);       
  },
};

Module.setStatus("Downloading...");

window.onerror = function (event) {
  // TODO: do not warn on ok events like simulating an infinite loop or exitStatus
  Module.setStatus("Exception thrown, see JavaScript console");
  spinnerElement.style.display = "none";
  Module.setStatus = function (text) {
    if (text) Module.printErr("[post-exception status] " + text);
  };
};
