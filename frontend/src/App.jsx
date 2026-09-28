import { useEffect, useState } from "react";
import "./App.css";

const API = "http://127.0.0.1:8765";

function App() {
  const [devices, setDevices] = useState([]);
  const [selected, setSelected] = useState([]);
  const [running, setRunning] = useState(false);
  const [loading, setLoading] = useState(true);
  const [message, setMessage] = useState("Connecting to audio engine...");
  const [volumes, setVolumes] = useState({ speaker1: 1, speaker2: 1 });

  async function loadDevices() {
    try {
      const response = await fetch(`${API}/devices`);

      if (!response.ok) {
        throw new Error("Failed to load devices");
      }

      const data = await response.json();
      setDevices(data.devices || []);
      setMessage("Select two output speakers.");
    } catch (error) {
      console.error(error);
      setMessage(
        "Cannot connect to C++ audio engine. Start AudioEngine.exe first."
      );
    } finally {
      setLoading(false);
    }
  }

  async function loadStatus() {
    try {
      const response = await fetch(`${API}/status`);
      if (!response.ok) return;

      const data = await response.json();
      setRunning(Boolean(data.running));
      setVolumes({
        speaker1: Number(data.cobraVolume ?? 1),
        speaker2: Number(data.miviVolume ?? 1),
      });
    } catch {
      // Engine may not be running yet.
    }
  }

  useEffect(() => {
    loadDevices();
    loadStatus();
  }, []);

  function toggleDevice(index) {
    if (running) return;

    setSelected((current) => {
      if (current.includes(index)) {
        return current.filter((item) => item !== index);
      }

      if (current.length >= 2) {
        return current;
      }

      return [...current, index];
    });
  }

  async function startEngine() {
    if (selected.length !== 2) {
      setMessage("Select exactly two speakers first.");
      return;
    }

    setMessage("Selecting speakers...");

    try {
      const selectResponse = await fetch(`${API}/select`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          speaker1: selected[0],
          speaker2: selected[1],
        }),
      });

      const selectData = await selectResponse.json();

      if (!selectResponse.ok) {
        throw new Error(selectData.error || "Speaker selection failed");
      }

      setMessage("Starting audio engine...");

      const startResponse = await fetch(`${API}/start`, {
        method: "POST",
      });

      const startData = await startResponse.json();

      if (!startResponse.ok) {
        throw new Error(startData.error || "Failed to start engine");
      }

      setRunning(true);
      setMessage("Audio is running on both speakers.");
    } catch (error) {
      console.error(error);
      setMessage(error.message);
    }
  }

  async function stopEngine() {
    try {
      const response = await fetch(`${API}/stop`, {
        method: "POST",
      });

      if (!response.ok) {
        throw new Error("Failed to stop engine");
      }

      setRunning(false);
      setMessage("Audio engine stopped.");
    } catch (error) {
      console.error(error);
      setMessage(error.message);
    }
  }

  async function changeVolume(speakerNumber, value) {
    const numericValue = Number(value);

    setVolumes((current) => ({
      ...current,
      [speakerNumber]: numericValue,
    }));

    try {
      await fetch(`${API}/volume`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          [speakerNumber]: numericValue,
        }),
      });
    } catch (error) {
      console.error(error);
    }
  }

  const selectedDevices = selected.map((index) =>
    devices.find((device) => device.index === index)
  );

  return (
    <div className="app">
      <div className="panel">
        <h1>Multi Speaker</h1>

        <p className="subtitle">
          Play Windows system audio through two Bluetooth speakers.
        </p>

        <section>
          <div className="section-header">
            <h2>Output Speakers</h2>
            <span>{selected.length}/2 selected</span>
          </div>

          {loading ? (
            <p className="message">Loading audio devices...</p>
          ) : (
            <div className="device-list">
              {devices.map((device) => {
                const checked = selected.includes(device.index);

                return (
                  <button
                    key={device.index}
                    className={`device ${checked ? "selected" : ""}`}
                    onClick={() => toggleDevice(device.index)}
                    disabled={running}
                  >
                    <span className={`checkbox ${checked ? "checked" : ""}`}>
                      {checked ? "✓" : ""}
                    </span>

                    <span className="device-name">
                      {device.name}
                    </span>
                  </button>
                );
              })}
            </div>
          )}
        </section>

        <section>
          <h2>Selected Speakers</h2>

          <div className="selected-list">
            {selectedDevices.map((device, index) => {
              if (!device) return null;

              const speakerNumber = index === 0 ? "speaker1" : "speaker2";

              return (
                <div className="speaker-card" key={device.index}>
                  <div className="speaker-title">
                    <strong>Speaker {index + 1}</strong>
                    <span>{device.name}</span>
                  </div>

                  <label>
                    Volume
                    <span>
                      {Math.round(volumes[speakerNumber] * 100)}%
                    </span>
                  </label>

                  <input
                    type="range"
                    min="0"
                    max="1"
                    step="0.01"
                    value={volumes[speakerNumber]}
                    onChange={(event) =>
                      changeVolume(
                        speakerNumber,
                        event.target.value
                      )
                    }
                  />
                </div>
              );
            })}
          </div>
        </section>

        <div className="controls">
          {!running ? (
            <button
              className="start-button"
              onClick={startEngine}
              disabled={selected.length !== 2}
            >
              Start
            </button>
          ) : (
            <button
              className="stop-button"
              onClick={stopEngine}
            >
              Stop
            </button>
          )}
        </div>

        <div className={`status ${running ? "running" : ""}`}>
          <span className="status-dot" />
          {message}
        </div>
      </div>
    </div>
  );
}

export default App;
