import os
import json
from datetime import datetime, timezone

from google import genai
from flask import Flask, jsonify, render_template, request

# Flask 앱 생성
app = Flask(__name__)

# Gemini API 설정 (Vercel 환경변수 사용)
API_KEY = os.getenv("GEMINI_API_KEY", "")
GEMINI_MODEL = "gemini-2.5-flash"

# 최신 센서 데이터를 메모리에 보관
_latest: dict = {}


def _decide_motors(temp: float, hum: float) -> tuple[bool, bool]:
    # 간단한 안전 규칙: 고온이면 팬 ON, 저습이면 펌프 ON
    fan_on = temp >= 28.0
    pump_on = hum <= 45.0
    return fan_on, pump_on


def _build_fallback_result(temp: float, hum: float, reason: str) -> str:
    fan_on, pump_on = _decide_motors(temp, hum)
    advice = "환기유지, 과습주의"
    if len(advice) > 20:
        advice = advice[:20]
    return json.dumps(
        {
            "fan": "ON" if fan_on else "OFF",
            "pump": "ON" if pump_on else "OFF",
            "advice": advice,
            "reason": reason[:80],
        },
        ensure_ascii=False,
    )


def _call_gemini(prompt: str) -> str:
    client = genai.Client(api_key=API_KEY)
    response = client.models.generate_content(
        model=GEMINI_MODEL,
        contents=prompt,
    )
    if not response.text:
        raise ValueError("Gemini response text is empty")
    return response.text


@app.route("/", methods=["GET"])
def index():
    return render_template("index.html")


@app.route("/api/latest", methods=["GET"])
def api_latest():
    return jsonify(_latest)


@app.route("/favicon.ico", methods=["GET"])
def favicon_ico():
    return "", 204


@app.route("/favicon.png", methods=["GET"])
def favicon_png():
    return "", 204


# (구) 헬스체크 엔드포인트 — 하위 호환용
@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok"})


@app.route("/sensor", methods=["POST"])
def sensor():
    try:
        data = request.get_json(force=True)
        if not data:
            return jsonify({"error": "JSON body is required"}), 400

        temp = data.get("temperature")
        hum = data.get("humidity")
        if temp is None or hum is None:
            return jsonify({"error": "temperature and humidity are required"}), 400

        print(f"[센서] 온도: {temp}, 습도: {hum}")

        prompt = (
            "아래 규칙을 반드시 지켜 한 줄 JSON만 출력하세요. "
            "추가 설명/코드블록/개행 금지. "
            f"입력값: temperature={temp}, humidity={hum}. "
            "출력 스키마: {\"fan\":\"ON|OFF\",\"pump\":\"ON|OFF\",\"advice\":\"20자 이내\"}. "
            "advice는 한국어 20자 이내로 작성하세요."
        )

        result = ""
        ai_error = None

        if API_KEY:
            try:
                result = _call_gemini(prompt)
            except Exception as e:
                ai_error = f"Gemini request failed: {e}"
        else:
            ai_error = "GEMINI_API_KEY is not set"

        if not result:
            result = _build_fallback_result(float(temp), float(hum), ai_error or "unknown")

        print(f"[AI 응답] {result}")

        # 최신 데이터 저장 (대시보드 표시용)
        _latest.update({
            "temperature": temp,
            "humidity": hum,
            "result": result,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        })

        response = {"result": result}
        if ai_error:
            response["fallback"] = True
            response["detail"] = ai_error
        return jsonify(response)

    except Exception as e:
        print("에러:", e)
        return jsonify({"error": str(e)}), 500


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)