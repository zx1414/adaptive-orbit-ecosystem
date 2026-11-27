const repository = "https://github.com/zx1414/adaptive-orbit-ecosystem";
const playerFeed = "https://api.github.com/repos/zx1414/adaptive-orbit-ecosystem/issues?state=open&labels=community-extension&sort=created&direction=desc&per_page=12";
const types = {
  mechanism: { label: "机制 Mod", guide: "MODS.md" },
  decision: { label: "决策模型", guide: "RESEARCH.md" },
  material: { label: "材质包", guide: "RESOURCE-PACKS.md" },
};

const catalog = document.querySelector("#catalog");
const result = document.querySelector("#result");
const loading = document.querySelector("#loading");
const filters = document.querySelector(".filters");
const buttons = [...filters.querySelectorAll("button")];

function node(tag, text, className) {
  const element = document.createElement(tag);
  if (text) element.textContent = text;
  if (className) element.className = className;
  return element;
}

function link(text, href, className) {
  const url = new URL(href, document.baseURI);
  if (url.protocol !== "https:" && (url.protocol !== "http:" || url.origin !== location.origin)) {
    throw new Error("Invalid catalog link");
  }
  const anchor = node("a", text, className);
  anchor.href = url.href;
  return anchor;
}

function renderCard(entry) {
  const type = types[entry.kind];
  const article = node("article", null, "card");
  article.id = entry.id;
  article.dataset.kind = entry.kind;

  const instructions = node("details");
  instructions.append(
    node("summary", "安装说明"),
    node("p", entry.install),
    link("开发指南", `${repository}/blob/main/docs/${type.guide}`, "text-link"),
  );

  const download = link("下载", entry.download, "button primary");
  download.setAttribute("aria-label", `下载${entry.title}`);
  if (new URL(download.href).origin === location.origin) download.download = "";

  const actions = node("div", null, "card-actions");
  actions.append(download, link("查看源文件", entry.source, "text-link"));
  article.append(
    node("span", type.label, "kind"),
    node("h3", entry.title),
    node("p", entry.summary),
    node("p", `${entry.author} · 扩展 ${entry.version} · ${entry.license}`, "meta"),
    node("p", `已验证游戏版本 ${entry.gameVersion}`, "meta"),
    instructions,
    actions,
  );
  return article;
}

function playerFields(body) {
  const sections = (body ?? "").split(/^### ([^\r\n]+)\r?\n/m);
  const fields = new Map();
  for (let i = 1; i < sections.length; i += 2) {
    const label = sections[i].trim();
    if (!fields.has(label)) fields.set(label, sections[i + 1].trim());
  }
  return fields;
}

function publicDownload(field) {
  for (const match of (field ?? "").matchAll(/https:\/\/[^\s<>()[\]]+/gi)) {
    try {
      const url = new URL(match[0]);
      if (!url.username && !url.password) return url;
    } catch {
      // The work's original post remains available when a link is malformed.
    }
  }
  return null;
}

function renderPlayerCard(issue) {
  const fields = playerFields(issue.body);
  const title = issue.title.replace(/^\[玩家扩展\]\s*/, "").trim() || `作品 #${issue.number}`;
  const category = fields.get("扩展类别") || "玩家扩展";
  const author = issue.user?.login ?? "GitHub 玩家";
  const version = fields.get("已试用的游戏版本");
  const summary = fields.get("作品介绍")?.replace(/\s+/g, " ");
  const article = node("article", null, "card");
  article.append(node("span", category, "kind"), node("h3", title));
  if (summary) article.append(node("p", summary.length > 200 ? `${summary.slice(0, 200)}…` : summary));
  article.append(node("p", version ? `${author} · 作者试用版本 ${version}` : author, "meta"));

  const actions = node("div", null, "card-actions");
  const downloadUrl = publicDownload(fields.get("下载地址"));
  if (downloadUrl) {
    const download = link("下载", downloadUrl, "button primary");
    download.setAttribute("aria-label", `下载${title}`);
    download.rel = "ugc nofollow";
    actions.append(download);
  }
  actions.append(link("查看发布与安装说明", `${repository}/issues/${issue.number}`, "text-link"));
  article.append(actions);
  return article;
}

async function loadPlayers() {
  const cards = document.querySelector("#player-cards");
  const status = document.querySelector("#player-status");
  status.textContent = "正在读取最新作品…";
  try {
    const response = await fetch(playerFeed, { cache: "no-store" });
    if (!response.ok) throw new Error("Player releases request failed");
    const issues = (await response.json()).filter((issue) => !issue.pull_request);
    cards.append(...issues.map(renderPlayerCard));
    status.hidden = issues.length > 0;
    status.textContent = "暂时还没有玩家作品。";
  } catch {
    status.textContent = "暂时无法加载玩家作品，请使用上方的 GitHub 列表。";
  }
}

function filter(kind) {
  let count = 0;
  for (const card of catalog.children) {
    card.hidden = kind !== "all" && card.dataset.kind !== kind;
    if (!card.hidden) count++;
  }
  for (const button of buttons) {
    button.setAttribute("aria-pressed", String(button.dataset.kind === kind));
  }
  result.textContent = `${count} 个扩展`;
  loading.hidden = count !== 0;
  loading.textContent = "这个类别暂时没有扩展。";
}

function revealLinkedCard() {
  const card = document.getElementById(location.hash.slice(1));
  if (card?.parentElement === catalog) {
    if (card.hidden) filter("all");
    card.scrollIntoView();
  }
}

async function loadCatalog() {
  try {
    const response = await fetch("community/catalog.json");
    if (!response.ok) throw new Error("Catalog request failed");
    const entries = await response.json();
    catalog.append(...entries.map(renderCard));
    filters.hidden = false;
    for (const button of buttons) {
      button.addEventListener("click", () => filter(button.dataset.kind));
    }
    filter("all");
    revealLinkedCard();
    window.addEventListener("hashchange", revealLinkedCard);
  } catch {
    loading.replaceChildren(
      "扩展目录暂时无法加载。",
      link("在 GitHub 浏览扩展文件", `${repository}/tree/main/docs/community/extensions`),
    );
  }
}

loadCatalog();
loadPlayers();
