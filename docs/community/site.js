const repository = "https://github.com/zx1414/adaptive-orbit-ecosystem";
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
