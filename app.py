from flask import Flask, render_template, request, jsonify
from datetime import datetime

app = Flask(__name__)


MAX_VOLTAGE = 6.0
MIN_VOLTAGE = 0.0

battery_data = {
    "voltage": 0,
    "current": 0,
    "power": 0,
    "temperature": 0,
    "soc": 0,
    "soh": 100,
    "cycles": 0,               
    "cutoff": False,
    "reason": "Normal",
    "status": "Waiting for data...",
    "last_update": "--"
}

@app.route('/')
def index():
    return render_template('index.html', data=battery_data)

@app.route('/data', methods=['POST'])
def data():
    global battery_data
    try:
        d = request.get_json()

        battery_data.update({
            "voltage": float(d.get("voltage", 0)),
            "current": float(d.get("current", 0)),
            "power": float(d.get("power", 0)),
            "temperature": float(d.get("temperature", 0)),
            "soc": float(d.get("soc", 0)),
            "soh": float(d.get("soh", 0)),
            "cycles": int(d.get("cycles", 0)),     
            "cutoff": d.get("cutoff", False),
            "reason": d.get("reason", "Normal"),
            "last_update": datetime.now().strftime("%H:%M:%S"),
            "status": "CUT-OFF ⚠️" if d.get("cutoff", False) else "Normal"
        })
        print("Data updated:", battery_data)
        return jsonify({"status": "ok"}), 200
    except Exception as e:
        print("Error:", e)
        return jsonify({"status": "error", "message": str(e)}), 400


@app.route('/latest')
def latest():
    return jsonify(battery_data)


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
