<h1 align="center">DualSpeakerSync</h1>

<p align="center">
  <strong>Windows multi-speaker audio synchronization using C++, WASAPI and React</strong>
</p>

<p align="center">
  Capture Windows system audio and play it simultaneously through multiple audio devices.
</p>

<hr>

<h2>🎯 Project Goal</h2>

<p>
DualSpeakerSync is a Windows-based audio synchronization project designed to
capture system audio and play it simultaneously through multiple Bluetooth
speakers or other Windows audio output devices.
</p>

<p>
The project combines a native C++ audio engine with a React-based control
interface. The C++ engine handles audio capture, buffering, resampling,
output rendering and delay compensation, while React provides the user
interface for controlling the engine.
</p>

<pre>
Netflix / YouTube / Spotify / Prime Video / Games
                        │
                        ▼
              ┌──────────────────┐
              │ WASAPI Loopback   │
              │     Capture       │
              └────────┬─────────┘
                       │
                       ▼
              ┌──────────────────┐
              │  C++ Audio       │
              │     Engine       │
              │                  │
              │ • Buffering      │
              │ • Resampling     │
              │ • Volume         │
              │ • Delay          │
              │ • Synchronizing  │
              └────────┬─────────┘
                       │
                 ┌─────┴─────┐
                 ▼           ▼
             Speaker 1   Speaker 2
</pre>

<h2>✨ Features</h2>

<h3>Current Features</h3>

<ul>
  <li>Windows system-audio capture using WASAPI Loopback</li>
  <li>Native C++ audio processing</li>
  <li>Windows audio-device enumeration</li>
  <li>Bluetooth speaker/headphone selection</li>
  <li>Simultaneous audio playback</li>
  <li>Per-output volume control</li>
  <li>Per-output delay configuration</li>
  <li>Initial audio buffering</li>
  <li>Ring-buffer based audio processing</li>
  <li>Audio resampling</li>
  <li>Basic synchronization infrastructure</li>
  <li>React + Vite control interface</li>
  <li>Local HTTP communication between React and C++</li>
</ul>

<h3>🚧 In Development</h3>

<ul>
  <li>Improved automatic synchronization</li>
  <li>Independent clock-drift correction</li>
  <li>More than two simultaneous output devices</li>
  <li>More accurate latency measurement</li>
  <li>Long-duration synchronization stability</li>
  <li>Improved Bluetooth-device handling</li>
</ul>

<h2>🛠️ Tech Stack</h2>

<table>
  <tr>
    <th>Component</th>
    <th>Technology</th>
  </tr>
  <tr>
    <td>Audio Engine</td>
    <td>C++17</td>
  </tr>
  <tr>
    <td>Windows Audio</td>
    <td>WASAPI / Windows Core Audio</td>
  </tr>
  <tr>
    <td>Audio Capture</td>
    <td>WASAPI Loopback</td>
  </tr>
  <tr>
    <td>Frontend</td>
    <td>React + Vite</td>
  </tr>
  <tr>
    <td>Communication</td>
    <td>Local HTTP API</td>
  </tr>
  <tr>
    <td>Networking</td>
    <td>Windows Winsock</td>
  </tr>
  <tr>
    <td>Compiler</td>
    <td>MSVC</td>
  </tr>
  <tr>
    <td>Build Environment</td>
    <td>Visual Studio Build Tools + Windows SDK</td>
  </tr>
</table>

<h2>🏗️ Architecture</h2>

<h3>1. WASAPI Loopback Capture</h3>

<p>
The application captures Windows system audio using WASAPI loopback.
The audio is captured from the Windows audio engine instead of directly
capturing from a Bluetooth speaker.
</p>

<pre>
Application
     │
     ▼
Windows Audio Engine
     │
     ▼
WASAPI Loopback
     │
     ▼
DualSpeakerSync
     │
     ├──────────────┐
     ▼              ▼
Speaker 1       Speaker 2
</pre>

<h3>2. C++ Audio Engine</h3>

<p>
The native audio engine is responsible for the actual audio pipeline.
</p>

<ul>
  <li>Audio-device enumeration</li>
  <li>WASAPI capture</li>
  <li>Audio buffering</li>
  <li>Audio format handling</li>
  <li>Resampling</li>
  <li>Output rendering</li>
  <li>Volume control</li>
  <li>Delay compensation</li>
  <li>Synchronization</li>
</ul>

<h3>3. React Control Interface</h3>

<p>
The React frontend communicates with the C++ engine through a local HTTP
server.
</p>

<pre>
┌──────────────────────┐
│     React Frontend   │
│                      │
│ Speaker Selection    │
│ Volume Control       │
│ Delay Control        │
│ Start / Stop         │
└──────────┬───────────┘
           │
           │ HTTP
           ▼
┌──────────────────────┐
│   C++ HTTP Server     │
│      Port 8765        │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│     AudioEngine      │
└──────────┬───────────┘
           │
       ┌───┴────┐
       ▼        ▼
   Speaker 1  Speaker 2
</pre>

<h2>🔊 Synchronization</h2>

<p>
Synchronizing multiple Bluetooth audio devices is one of the main technical
challenges of this project.
</p>

<p>
Different output devices can have different:
</p>

<ul>
  <li>Bluetooth latency</li>
  <li>Internal buffering</li>
  <li>Hardware clock rates</li>
  <li>Driver behavior</li>
  <li>Processing latency</li>
</ul>

<h3>Initial Buffering</h3>

<p>
Audio is buffered before playback starts. This provides the output pipeline
with a stable amount of audio before rendering begins.
</p>

<h3>Delay Compensation</h3>

<p>
Individual outputs can be assigned different delays to compensate for
different device latencies.
</p>

<pre>
Speaker A → 0 ms
Speaker B → 80 ms
</pre>

<h3>Ring Buffers</h3>

<p>
Audio is stored in ring buffers between the capture and rendering stages.
This helps separate the capture timing from the output-device timing.
</p>

<h3>Resampling</h3>

<p>
The project includes audio-resampling infrastructure that can adjust the
playback rate when required.
</p>

<h2>📁 Project Structure</h2>

<pre>
Multi Speaker/
│
├── audioEngine/
│   │
│   ├── AudioEngine.cpp
│   ├── AudioEngine.h
│   ├── HttpServer.cpp
│   ├── main.cpp
│   │
│   ├── sync_test.cpp
│   ├── capture_test.cpp
│   ├── multi_output_test.cpp
│   ├── multi_output_sync_test.cpp
│   ├── latency_measurement.cpp
│   ├── ...
│   │
│   └── build/
│
├── frontend/
│   ├── src/
│   ├── public/
│   ├── package.json
│   ├── vite.config.js
│   └── ...
│
├── .gitignore
└── README.md
</pre>

<p>
The <code>audioEngine</code> directory contains the main engine as well as
experimental programs used during audio capture, latency and synchronization
testing.
</p>

<h2>💻 Requirements</h2>

<h3>Operating System</h3>

<ul>
  <li>Windows 10</li>
  <li>Windows 11</li>
</ul>

<h3>C++ Development Environment</h3>

<ul>
  <li>Visual Studio Build Tools</li>
  <li>Desktop development with C++</li>
  <li>MSVC compiler</li>
  <li>Windows SDK</li>
</ul>

<h3>Frontend</h3>

<ul>
  <li>Node.js</li>
  <li>npm</li>
</ul>

<h2>🚀 Build the C++ Audio Engine</h2>

<p>
Open a <strong>Developer PowerShell for Visual Studio</strong>.
</p>

<pre><code>cd "E:\My Files\Projects\Multi Speaker\audioEngine"</code></pre>

<p>Create the build directory:</p>

<pre><code>mkdir build -ErrorAction SilentlyContinue</code></pre>

<p>Compile the engine:</p>

<pre><code>cl /EHsc /std:c++17 /Fo"build\" AudioEngine.cpp HttpServer.cpp main.cpp /Fe"build\MultiSpeakerSync.exe" /link ole32.lib avrt.lib ws2_32.lib</code></pre>

<p>
The executable will be generated inside:
</p>

<pre><code>audioEngine/build/</code></pre>

<h2>🌐 Run the React Frontend</h2>

<p>Navigate to the frontend:</p>

<pre><code>cd "E:\My Files\Projects\Multi Speaker\frontend"</code></pre>

<p>Install dependencies:</p>

<pre><code>npm install</code></pre>

<p>Start the development server:</p>

<pre><code>npm run dev</code></pre>

<p>
Vite will provide a local development URL, usually:
</p>

<pre><code>http://localhost:5173</code></pre>

<h2>▶️ Run the Application</h2>

<p>First start the C++ audio engine:</p>

<pre><code>cd "E:\My Files\Projects\Multi Speaker\audioEngine"
.\build\MultiSpeakerSync.exe</code></pre>

<p>Then start the React frontend:</p>

<pre><code>cd "..\frontend"
npm run dev</code></pre>

<p>
Open the Vite URL in your browser.
The React application communicates with the native C++ engine through:
</p>

<pre><code>http://localhost:8765</code></pre>

<h2>🔌 HTTP API</h2>

<h3>Get Audio Devices</h3>

<pre><code>GET /devices</code></pre>

<p>Returns available Windows audio devices.</p>

<pre><code>[
  {
    "index": 0,
    "name": "Headphones (SG-SM4172 COBRA)"
  },
  {
    "index": 1,
    "name": "Mivi Play Black"
  }
]</code></pre>

<h3>Start Playback</h3>

<pre><code>POST /start</code></pre>

<h3>Stop Playback</h3>

<pre><code>POST /stop</code></pre>

<h3>Select Speakers</h3>

<pre><code>POST /select</code></pre>

<pre><code>{
  "speaker1": 0,
  "speaker2": 1
}</code></pre>

<h3>Volume</h3>

<pre><code>POST /volume</code></pre>

<pre><code>{
  "speaker1": 1.0,
  "speaker2": 0.8
}</code></pre>

<h3>Delay</h3>

<pre><code>POST /delay</code></pre>

<pre><code>{
  "speaker1": 0,
  "speaker2": 80
}</code></pre>

<p>
Delay values are specified in milliseconds.
</p>

<h3>Status</h3>

<pre><code>GET /status</code></pre>

<p>
Returns the current state and configuration of the audio engine.
</p>

<h2>🧪 Experimental Programs</h2>

<p>
The repository contains several standalone programs used during development
and testing.
</p>

<ul>
  <li><code>sync_test.cpp</code></li>
  <li><code>capture_test.cpp</code></li>
  <li><code>multi_output_test.cpp</code></li>
  <li><code>multi_output_sync_test.cpp</code></li>
  <li><code>latency_measurement.cpp</code></li>
</ul>

<p>
These files are intentionally retained because they document the experimental
development of the audio engine and synchronization system.
</p>

<h2>🎧 Tested Audio Devices</h2>

<p>
Development testing has included Windows audio devices such as:
</p>

<ul>
  <li>Bluetooth headphones</li>
  <li>Bluetooth speakers</li>
  <li>Realtek audio output</li>
  <li>Display audio devices</li>
</ul>

<p>Example devices used during development:</p>

<pre>
Headphones (SG-SM4172 COBRA)
Mivi Play Black
AAA (HD Audio Driver for Display Audio)
Speakers (Realtek(R) Audio)
</pre>

<p>
Available devices depend on the user's Windows configuration.
</p>

<h2>⚠️ Current Limitations</h2>

<ul>
  <li>
    Bluetooth devices can have different inherent audio latency.
  </li>
  <li>
    Long-duration playback can introduce clock drift between devices.
  </li>
  <li>
    Different speaker combinations may require different delay calibration.
  </li>
  <li>
    Bluetooth connections can introduce variable latency.
  </li>
  <li>
    Device disconnect/reconnect handling is still under development.
  </li>
  <li>
    Automatic long-term synchronization is still being improved.
  </li>
  <li>
    The current HTTP API is based on the initial two-output implementation.
  </li>
</ul>

<h2>🗺️ Roadmap</h2>

<h3>Phase 1 — Basic Playback</h3>

<ul>
  <li>✅ Windows audio capture</li>
  <li>✅ WASAPI loopback</li>
  <li>✅ Audio-device enumeration</li>
  <li>✅ Multiple output playback</li>
  <li>✅ Basic buffering</li>
  <li>✅ Volume control</li>
  <li>✅ Manual delay compensation</li>
</ul>

<h3>Phase 2 — Synchronization</h3>

<ul>
  <li>✅ Ring-buffer based playback</li>
  <li>✅ Resampling infrastructure</li>
  <li>✅ Initial synchronization testing</li>
  <li>⬜ Independent correction for each output</li>
  <li>⬜ Automatic clock-drift correction</li>
  <li>⬜ Improved latency measurement</li>
  <li>⬜ Long-duration synchronization testing</li>
</ul>

<h3>Phase 3 — Multi-Speaker Support</h3>

<ul>
  <li>⬜ Dynamic speaker list</li>
  <li>⬜ More than two simultaneous outputs</li>
  <li>⬜ Independent buffer per output</li>
  <li>⬜ Independent resampler per output</li>
  <li>⬜ Independent synchronization correction</li>
  <li>⬜ Per-speaker configuration</li>
</ul>

<h3>Phase 4 — Production Improvements</h3>

<ul>
  <li>⬜ Automatic device reconnect</li>
  <li>⬜ Improved error reporting</li>
  <li>⬜ Background audio engine</li>
  <li>⬜ Windows startup support</li>
  <li>⬜ Improved UI</li>
  <li>⬜ Application packaging</li>
  <li>⬜ Performance optimization</li>
</ul>

<h2>🔐 Privacy</h2>

<p>
DualSpeakerSync is designed as a local application.
</p>

<p>
The core audio processing happens locally on the Windows machine. The React
frontend communicates with the C++ engine through the local HTTP server:
</p>

<pre><code>localhost:8765</code></pre>

<p>
No cloud backend is required for the core audio functionality.
</p>

<h2>📌 Why C++?</h2>

<p>
The audio engine uses C++ because the project requires direct access to
Windows audio APIs and low-level audio processing.
</p>

<p>C++ provides access to:</p>

<ul>
  <li>WASAPI</li>
  <li>Windows Core Audio</li>
  <li>Audio buffers</li>
  <li>Windows audio endpoints</li>
  <li>Native threading</li>
  <li>Low-level timing</li>
  <li>Audio synchronization mechanisms</li>
</ul>

<p>
React is used as the control interface, while the C++ application handles
the actual audio pipeline.
</p>

<h2>📄 License</h2>

<p>
This project is currently under development.
</p>

<p>
License information will be added when the project is ready for public
distribution.
</p>

<h2>👨‍💻 Author</h2>

<p>
<strong>Tumansh Vij</strong>
</p>

<p>
DualSpeakerSync is a personal project exploring:
</p>

<ul>
  <li>Windows audio programming</li>
  <li>WASAPI</li>
  <li>Bluetooth audio</li>
  <li>Real-time audio processing</li>
  <li>Multi-device synchronization</li>
  <li>C++</li>
  <li>React</li>
  <li>Native application architecture</li>
</ul>
