import { createCanvas, loadImage } from "canvas";
import fs from "fs";
import path from "path";
import * as cs from "./index.js";

export const linkBackgroundDefault = "https://f58-zpg-r.zdn.vn/jpg/5044485622965252364/799cd26798cc24927ddd.jpg";
export const linkBackgroundDefaultZalo = "https://cover-talk.zadn.vn/default";

export async function getLinkBackgroundDefault(userInfo) {
  let backgroundImage;
  try {
    if (userInfo.cover && userInfo.cover !== linkBackgroundDefaultZalo) {
      backgroundImage = await loadImage(userInfo.cover);
    } else {
      backgroundImage = await loadImage(linkBackgroundDefault);
    }
  } catch (error) {
    backgroundImage = await loadImage(linkBackgroundDefault);
  }
  return backgroundImage;
}

async function createImage(userInfo, message, fileName) {
  const width = 1000;
  const height = 300;
  const canvas = createCanvas(width, height);
  const ctx = canvas.getContext("2d");

  let backgroundImage;
  let typeImage = -1;
  let fluent = 0.8;
  if (fileName.includes("welcome")) {
    typeImage = 0;
    fluent = 0.6;
  } else if (fileName.includes("goodbye")) {
    typeImage = 1;
    fluent = 0.6;
  } else if (["blocked", "kicked", "kicked_spam"].some(keyword => fileName.includes(keyword))) {
    typeImage = 2;
    fluent = 0.85;
  }
  try {
    backgroundImage = await getLinkBackgroundDefault(userInfo);

    ctx.drawImage(backgroundImage, 0, 0, width, height);

    const overlay = ctx.createLinearGradient(0, 0, 0, height);
    overlay.addColorStop(0, `rgba(30, 30, 53, ${fluent})`);
    overlay.addColorStop(0.5, `rgba(26, 37, 71, ${fluent})`);
    overlay.addColorStop(1, `rgba(19, 27, 54, ${fluent})`);

    ctx.fillStyle = overlay;
    ctx.fillRect(0, 0, width, height);

  } catch (error) {
    console.error("Lỗi khi xử lý background:", error);
    const backgroundGradient = ctx.createLinearGradient(0, 0, 0, height);
    backgroundGradient.addColorStop(0, "#1E1E35");
    backgroundGradient.addColorStop(0.5, "#1A2547");
    backgroundGradient.addColorStop(1, "#131B36");

    ctx.fillStyle = backgroundGradient;
    ctx.fillRect(0, 0, width, height);
  }

  let xAvatar = 118;
  let widthAvatar = 162;
  let heightAvatar = 162;
  let yAvatar = height / 2 - heightAvatar / 2;

  let gradientColors;
  if (typeImage === 0) {
    gradientColors = [
      "#00ffcc", // Xanh cyan neon
      "#00ff95", // Xanh mint neon
      "#00ff80", // Xanh spring neon
      "#1aff8c", // Xanh lá neon sáng
      "#33ff99", // Xanh aqua neon
    ];
  } else if (typeImage === 1) {
    gradientColors = [
      "#FFFFFF", // Trắng
      "#F0F0F0", // Xám rất nhạt sáng hơn
      "#FAFAFF", // Ghost white sáng hơn
      "#F8FBFF", // Alice blue sáng hơn
      "#EAEAFF", // Lavender sáng hơn
      "#FFF5FA", // Lavender blush sáng hơn
      "#FFFFFF"  // Trắng
    ];
  } else if (typeImage === 2) {
    gradientColors = [
      "#ff0000", // Đỏ tươi thuần
      "#ff1111", // Đỏ tươi sáng
      "#ff2200", // Đỏ cam tươi 
      "#ff0022", // Đỏ tươi đậm
      "#ff3300"  // Đỏ cam sáng
    ];
  } else {
    gradientColors = [
      "#FF1493", // Deep pink
      "#FF69B4", // Hot pink
      "#FFD700", // Gold
      "#FFA500", // Orange
      "#FF8C00", // Dark orange
      "#00FF7F", // Spring green
      "#40E0D0"  // Turquoise
    ];
  }
  const shuffledColors = [...gradientColors].sort(() => Math.random() - 0.5);

  const userAvatarUrl = userInfo.avatar;
  if (userAvatarUrl && cs.isValidUrl(userAvatarUrl)) {
    try {
      const avatar = await loadImage(userAvatarUrl);

      const borderWidth = 6;
      const gradient = ctx.createLinearGradient(
        xAvatar - widthAvatar / 2 - borderWidth,
        yAvatar - borderWidth,
        xAvatar + widthAvatar / 2 + borderWidth,
        yAvatar + heightAvatar + borderWidth
      );

      shuffledColors.forEach((color, index) => {
        gradient.addColorStop(index / (shuffledColors.length - 1), color);
      });

      ctx.save();
      ctx.beginPath();
      ctx.arc(xAvatar, height / 2, widthAvatar / 2 + borderWidth, 0, Math.PI * 2, true);
      ctx.fillStyle = gradient;
      ctx.fill();

      // Vẽ avatar
      ctx.beginPath();
      ctx.arc(xAvatar, height / 2, widthAvatar / 2, 0, Math.PI * 2, true);
      ctx.clip();
      ctx.drawImage(avatar, xAvatar - widthAvatar / 2, yAvatar, widthAvatar, heightAvatar);
      ctx.restore();

      // Vẽ đường thẳng màu trắng bên phải avatar
      ctx.beginPath();
      ctx.moveTo(xAvatar + widthAvatar / 2 + borderWidth + 30, yAvatar + 30);
      ctx.lineTo(xAvatar + widthAvatar / 2 + borderWidth + 30, yAvatar + heightAvatar - 30);
      ctx.strokeStyle = "white";
      ctx.lineWidth = 2;
      ctx.stroke();
    } catch (error) {
      console.error("Lỗi load avatar:", error);
    }
  } else {
    console.error("URL avatar Không hợp lệ:", userAvatarUrl);
  }

  let x1 = xAvatar - widthAvatar / 2 + widthAvatar;
  let x2 = x1 + (width - x1) / 2 - 5;
  let y1 = 86;

  // Tạo gradient cho title
  const titleGradient = ctx.createLinearGradient(x2 - 150, y1 - 30, x2 + 150, y1);
  shuffledColors.slice(0, 3).forEach((color, index) => {
    titleGradient.addColorStop(index / 2, color);
  });
  ctx.fillStyle = titleGradient;
  ctx.textAlign = "center";
  ctx.font = "bold 36px BeVietnamPro";
  ctx.fillText(message.title, x2, y1);

  // Gradient cho userName
  let y2 = y1 + 50;
  const userNameGradient = ctx.createLinearGradient(x2 - 150, y2 - 30, x2 + 150, y2);
  shuffledColors.slice(2, 5).forEach((color, index) => {
    userNameGradient.addColorStop(index / 2, color);
  });
  ctx.fillStyle = userNameGradient;
  ctx.font = "bold 36px BeVietnamPro";
  ctx.fillText(message.userName, x2, y2);

  // Gradient cho subtitle
  let y3 = y2 + 45;
  const subtitleGradient = ctx.createLinearGradient(x2 - 150, y3 - 30, x2 + 150, y3);
  shuffledColors.slice(1, 4).forEach((color, index) => {
    subtitleGradient.addColorStop(index / 2, color);
  });
  ctx.fillStyle = subtitleGradient;
  ctx.font = "32px BeVietnamPro";
  ctx.fillText(message.subtitle, x2, y3);

  // Gradient cho author
  let y4 = y3 + 45;
  const authorGradient = ctx.createLinearGradient(x2 - 150, y4 - 30, x2 + 150, y4);
  shuffledColors.slice(3, 6).forEach((color, index) => {
    authorGradient.addColorStop(index / 2, color);
  });
  ctx.fillStyle = authorGradient;
  ctx.font = "bold 32px BeVietnamPro";
  ctx.fillText(message.author, x2, y4);

  const filePath = path.resolve(`./assets/temp/${fileName}`);
  const out = fs.createWriteStream(filePath);
  const stream = canvas.createPNGStream();
  stream.pipe(out);
  return new Promise((resolve, reject) => {
    out.on("finish", () => resolve(filePath));
    out.on("error", reject);
  });
}

export async function createWelcomeImage(userInfo, groupName, groupType, userActionName, isAdmin) {
  const userName = userInfo.name || "";
  const authorText = userActionName === userName ? "Tham Gia Trực Tiếp Hoặc Được Mời" : `Duyệt bởi ${userActionName}`;
  return createImage(
    userInfo,
    {
      title: `${groupName}`,
      userName: `Chào mừng ${isAdmin ? "Đại Ca " : ""}${userName}`,
      subtitle: `Đã Tham Gia ${groupType ? (groupType === 2 ? "Cộng Đồng" : "Nhóm") : "Nhóm"}`,
      author: `${authorText}`,
    },
    `welcome_${Date.now()}.png`
  );
}

export async function createGoodbyeImage(userInfo, groupName, groupType, isAdmin) {
  const userName = userInfo.name || "";
  return createImage(
    userInfo,
    {
      title: "Member Left The Group",
      userName: `${isAdmin ? "Đại Ca " : ""}${userName}`,
      subtitle: `Vừa rời khỏi ${groupType ? (groupType === 2 ? "Cộng Đồng" : "Nhóm") : "Nhóm"}`,
      author: `${groupName}`
    },
    `goodbye_${Date.now()}.png`
  );
}

export async function createKickImage(userInfo, groupName, groupType, gender, userActionName, isAdmin) {
  const userName = userInfo.name || "";
  const genderText = gender === 0 ? "Thằng" : gender === 1 ? "Con" : "Thằng";
  let userNameText = isAdmin ? `Đại Ca ${userName}` : `${genderText} Oắt Con ${userName}`;
  return createImage(
    userInfo,
    {
      title: `Kicked Out Member`,
      userName: `${userNameText}`,
      subtitle: `Đã Bị ${userActionName} Sút Khỏi ${groupType ? (groupType === 2 ? "Cộng Đồng" : "Nhóm") : "Nhóm"}`,
      author: `${groupName}`,
    },
    `kicked_${Date.now()}.png`
  );
}

export async function createBlockImage(userInfo, groupName, groupType, gender, userActionName, isAdmin) {
  const userName = userInfo.name || "";
  const genderText = gender === 0 ? "Thằng" : gender === 1 ? "Con" : "Thằng";
  let userNameText = isAdmin ? `Đại Ca ${userName}` : `${genderText} Oắt Con ${userName}`;
  return createImage(
    userInfo,
    {
      title: `Blocked Out Member`,
      userName: `${userNameText}`,
      subtitle: `Đã Bị ${userActionName} Chặn Khỏi ${groupType ? (groupType === 2 ? "Cộng Đồng" : "Nhóm") : "Nhóm"}`,
      author: `${groupName}`,
    },
    `blocked_${Date.now()}.png`
  );
}

export async function createBlockSpamImage(userInfo, groupName, groupType, gender) {
  const userName = userInfo.name || "";
  const genderText = gender === 0 ? "Thằng" : gender === 1 ? "Con" : "Thằng";
  return createImage(
    userInfo,
    {
      title: `Blocked Out Spam Member`,
      userName: `${genderText} Oắt Con ${userName}`,
      subtitle: `Do spam đã bị chặn khỏi ${groupType ? (groupType === 2 ? "Cộng Đồng" : "Nhóm") : "Nhóm"}`,
      author: `${groupName}`,
    },
    `blocked_spam_${Date.now()}.png`
  );
}

export async function createBlockSpamLinkImage(userInfo, groupName, groupType, gender) {
  const userName = userInfo.name || "";
  const genderText = gender === 0 ? "Thằng" : gender === 1 ? "Con" : "Thằng";
  return createImage(
    userInfo,
    {
      title: `Blocked Out Spam Link Member`,
      userName: `${genderText} Oắt Con ${userName}`,
      subtitle: `Do spam link đã bị chặn khỏi ${groupType ? (groupType === 2 ? "Cộng Đồng" : "Nhóm") : "Nhóm"}`,
      author: `${groupName}`,
    },
    `blocked_spam_link_${Date.now()}.png`
  );
}
