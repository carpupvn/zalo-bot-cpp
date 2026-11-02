import json

# Đọc file export từ J2TEAM
with open("chat.zalo.me_03-10-2026.json", "r", encoding="utf-8") as f:
    data = json.load(f)

# Các cookie cần giữ
needed = {"zpsid", "zpw_sek", "__zi", "app.event.zalo.me", "_zlang"}

# Tạo chuỗi cookie
cookie_parts = []
for c in data["cookies"]:
    if c["name"] in needed:
        cookie_parts.append(f'{c["name"]}={c["value"]}')

cookie_string = "; ".join(cookie_parts)

# IMEI và User-Agent bạn tự điền (lấy từ Console)
config = {
    "cookie": cookie_string,
    "imei": "DÁN_IMEI_MỚI_VÀO_ĐÂY",
    "user_agent": "DÁN_USER_AGENT_MỚI_VÀO_ĐÂY"
}

with open("config.json", "w", encoding="utf-8") as f:
    json.dump(config, f, ensure_ascii=False, indent=2)

print("Đã tạo config.json thành công!")