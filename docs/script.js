// data-collect-system page: copy buttons, mobile menu, active nav link,
// and the interactive codebase graph. The page works without this script.
(function () {
  "use strict";

  // ---- copy buttons on code blocks ----
  function fallbackCopy(text) {
    var ta = document.createElement("textarea");
    ta.value = text;
    ta.setAttribute("readonly", "");
    ta.style.position = "fixed";
    ta.style.opacity = "0";
    document.body.appendChild(ta);
    ta.select();
    try { document.execCommand("copy"); } catch (e) { /* ignore */ }
    document.body.removeChild(ta);
  }
  document.querySelectorAll(".code pre").forEach(function (pre) {
    var btn = document.createElement("button");
    btn.type = "button";
    btn.className = "copy-btn";
    btn.textContent = "Copy";
    btn.setAttribute("aria-label", "Copy code to clipboard");
    btn.addEventListener("click", function () {
      var text = pre.innerText.replace(/\n$/, "");
      var done = function () {
        btn.textContent = "Copied";
        btn.classList.add("ok");
        setTimeout(function () { btn.textContent = "Copy"; btn.classList.remove("ok"); }, 1500);
      };
      if (navigator.clipboard && window.isSecureContext) {
        navigator.clipboard.writeText(text).then(done, function () { fallbackCopy(text); done(); });
      } else {
        fallbackCopy(text);
        done();
      }
    });
    pre.parentNode.appendChild(btn);
  });

  // ---- mobile menu ----
  var toggle = document.querySelector(".nav-toggle");
  var links = document.getElementById("nav-links");
  if (toggle && links) {
    toggle.addEventListener("click", function () {
      var open = links.classList.toggle("open");
      toggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
    links.addEventListener("click", function (e) {
      if (e.target.tagName === "A") {
        links.classList.remove("open");
        toggle.setAttribute("aria-expanded", "false");
      }
    });
  }

  // ---- highlight the nav link of the section in view ----
  if ("IntersectionObserver" in window && links) {
    var map = {};
    links.querySelectorAll("a").forEach(function (a) { map[a.getAttribute("href").slice(1)] = a; });
    var obs = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        if (en.isIntersecting && map[en.target.id]) {
          Object.keys(map).forEach(function (k) { map[k].classList.remove("active"); });
          map[en.target.id].classList.add("active");
        }
      });
    }, { rootMargin: "-40% 0px -55% 0px" });
    Object.keys(map).forEach(function (id) {
      var el = document.getElementById(id);
      if (el) obs.observe(el);
    });
  }

  // ---- codebase graph ----
  var svg = document.getElementById("codegraph");
  var info = document.getElementById("graph-info");
  if (!svg || !info) return;

  var nodes = {};
  svg.querySelectorAll(".node").forEach(function (n) { nodes[n.getAttribute("data-id")] = n; });
  var edges = Array.prototype.slice.call(svg.querySelectorAll(".edge"));
  var defaultInfo = info.innerHTML;
  var pinned = null;

  function short(id) { return id.replace(/^(bin|ext)\//, ""); }
  function esc(s) {
    return s.replace(/[&<>"]/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c];
    });
  }
  function list(arr) {
    return arr.length ? arr.map(function (x) { return "<code>" + esc(short(x)) + "</code>"; }).join(", ") : "none";
  }

  function clear() {
    svg.classList.remove("focusing");
    Object.keys(nodes).forEach(function (k) { nodes[k].classList.remove("sel", "dep", "rdep"); });
    edges.forEach(function (e) { e.classList.remove("in", "out"); });
    info.innerHTML = defaultInfo;
  }

  function focus(id) {
    clear();
    var node = nodes[id];
    if (!node) return;
    svg.classList.add("focusing");
    node.classList.add("sel");
    var outs = [], ins = [];
    edges.forEach(function (e) {
      var f = e.getAttribute("data-from"), t = e.getAttribute("data-to");
      if (f === id) { e.classList.add("out"); if (nodes[t]) { nodes[t].classList.add("dep"); outs.push(t); } }
      if (t === id) { e.classList.add("in"); if (nodes[f]) { nodes[f].classList.add("rdep"); ins.push(f); } }
    });
    var files = node.getAttribute("data-files");
    var isBin = id.indexOf("bin/") === 0;
    info.innerHTML =
      "<strong><code>" + esc(id) + "</code></strong>" + (files ? " <span class=\"gi-meta\" style=\"display:inline\">(" + esc(files) + ")</span>" : "") +
      "<br>" + esc(node.getAttribute("data-desc")) +
      "<span class=\"gi-meta\">" + (isBin ? "Built from: " : "Includes / uses: ") + list(outs) +
      (isBin ? "" : " &middot; Included by: " + list(ins)) + "</span>";
  }

  Object.keys(nodes).forEach(function (id) {
    var n = nodes[id];
    n.addEventListener("mouseenter", function () { if (!pinned) focus(id); });
    n.addEventListener("mouseleave", function () { if (!pinned) clear(); });
    n.addEventListener("focus", function () { if (!pinned) focus(id); });
    n.addEventListener("blur", function () { if (!pinned) clear(); });
    n.addEventListener("click", function (ev) {
      ev.stopPropagation();
      if (pinned === id) { pinned = null; clear(); }
      else { pinned = id; focus(id); }
    });
    n.addEventListener("keydown", function (ev) {
      if (ev.key === "Enter" || ev.key === " ") { ev.preventDefault(); n.dispatchEvent(new MouseEvent("click")); }
      if (ev.key === "Escape") { pinned = null; clear(); }
    });
  });
  svg.addEventListener("click", function () { if (pinned) { pinned = null; clear(); } });
})();
