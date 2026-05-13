import os

import requests
from flask import Flask, jsonify, request

# Flask 앱 생성
app = Flask(__name__)

# Gemini API 설정 (Vercel 환경변수 사용)
API_KEY = os.getenv("GEMINI_API_KEY", "")
GEMINI_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-latest:generateContent"


@app.route("/", methods=["GET"])
def health():
    return jsonify({"status": "ok", "message": "Flask on Vercel"})


@app.route("/sensor", methods=["POST"])
def sensor():
    try:
        if not API_KEY:
            return jsonify({"error": "GEMINI_API_KEY is not set"}), 500

        data = request.get_json(force=True)
        if not data:
            return jsonify({"error": "JSON body is required"}), 400

        temp = data.get("temperature")
        hum = data.get("humidity")
        if temp is None or hum is None:
            return jsonify({"error": "temperature and humidity are required"}), 400

        print(f"[센서] 온도: {temp}, 습도: {hum}")

        prompt = (
            f"현재 온도 {temp}도, 습도 {hum}%입니다. "
            "이 환경에 맞는 짧은 어린이 옷차림 조언을 20자 이내 한국어로 해줘."
        )

        headers = {
            "Content-Type": "application/json",
            "x-goog-api-key": API_KEY,
        }
        body = {
            "contents": [
                {
                    "parts": [
                        {"text": prompt},
                    ]
                }
            ]
        }

        res = requests.post(GEMINI_URL, headers=headers, json=body, timeout=10)
        res.raise_for_status()
        result_json = res.json()
        result = result_json["candidates"][0]["content"]["parts"][0]["text"]

        print(f"[AI 응답] {result}")
        return jsonify({"result": result})

    except requests.RequestException as e:
        print("Gemini 요청 에러:", e)
        return jsonify({"error": "Gemini request failed", "detail": str(e)}), 502
    except (KeyError, IndexError, TypeError) as e:
        print("Gemini 응답 파싱 에러:", e)
        return jsonify({"error": "Invalid Gemini response", "detail": str(e)}), 502
    except Exception as e:
        print("에러:", e)
        return jsonify({"error": str(e)}), 500


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)