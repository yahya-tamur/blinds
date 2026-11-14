// Shared by the pages (served as /dropdown.js)
const dropdowns = [];
document.addEventListener("click", e => {
  for (const dd of dropdowns) if (!dd.contains(e.target)) dd.close();
});

// Dropdowns in the page's own style, in place of <select>, whose list can't be styled. A button shows the choice;
// clicking it opens a list in the same rounded popover as the time picker. Arrow keys, Home/End, Enter and
// Escape work as in a <select>. Returns an object whose .value is the chosen option's value
// (.set(v) changes it without calling onChange). Used by index.html and update.html.
function makeDropdown(el, options, initial, onChange) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "field dropdown-button";
  button.setAttribute("aria-haspopup", "listbox");
  button.setAttribute("aria-expanded", "false");
  button.setAttribute("aria-label", el.getAttribute("aria-label"));
  const list = document.createElement("div");
  list.className = "popover dropdown-list";
  list.setAttribute("role", "listbox");
  list.hidden = true;
  const dd = { value: initial };
  const items = options.map(({ value, label }) => {
    const item = document.createElement("button");
    item.type = "button";
    item.setAttribute("role", "option");
    item.textContent = label;
    item.onclick = () => { choose(value); show(false); button.focus(); };
    return item;
  });
  list.append(...items);
  el.append(button, list);
  const index = () => options.findIndex(o => o.value === dd.value);
  function render() {
    button.textContent = options[index()].label;
    items.forEach((item, i) => item.setAttribute("aria-selected", i === index()));
  }
  function choose(value) {
    if (value === dd.value) return;
    dd.value = value;
    render();
    onChange();
  }
  function show(open) {
    list.hidden = !open;
    button.setAttribute("aria-expanded", open);
    if (open) {
      for (const other of dropdowns) if (other !== dd) other.close();
      const item = items[index()];
      list.scrollTop = item.offsetTop - (list.clientHeight - item.offsetHeight) / 2;   // the choice in the middle
      item.focus({ preventScroll: true });
    }
  }
  button.onclick = () => show(list.hidden);
  button.addEventListener("keydown", e => {
    if (["ArrowDown", "ArrowUp", "Enter", " "].includes(e.key)) {
      e.preventDefault();
      show(true);
    }
  });
  list.addEventListener("keydown", e => {
    const i = items.indexOf(document.activeElement);
    const to = { ArrowDown: i + 1, ArrowUp: i - 1, Home: 0, End: items.length - 1 }[e.key];
    if (to !== undefined) {
      e.preventDefault();
      items[Math.max(0, Math.min(items.length - 1, to))].focus();
    } else if (e.key === "Escape" || e.key === "Tab") {
      if (e.key === "Escape") e.preventDefault();
      show(false);
      button.focus();
    }
  });
  dd.close = () => { if (!list.hidden) show(false); };
  dd.contains = node => el.contains(node);
  dd.set = value => { if (options.some(o => o.value === value)) { dd.value = value; render(); } };   // without onChange
  dropdowns.push(dd);
  render();
  return dd;
}
