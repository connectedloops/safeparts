import { useEffect } from "react";

type Tab = "split" | "combine";

type ShortcutHandlers = {
  tab: Tab;
  setTab: (tab: Tab) => void;
  focusTab: (tab: Tab) => void;
  helpOpen: boolean;
  openHelp: () => void;
  closeHelp: () => void;
  keytipsActive: boolean;
  showKeytips: () => void;
  hideKeytips: () => void;
};

function isEditableTarget(target: EventTarget | null): boolean {
  if (!target || !(target instanceof HTMLElement)) return false;
  const tag = target.tagName;
  if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT") return true;
  return target.isContentEditable;
}

function clickSubmit(tab: Tab) {
  const panelId = tab === "split" ? "split-panel" : "combine-panel";
  const panel = document.getElementById(panelId);
  const btn = panel?.querySelector<HTMLButtonElement>(
    'button[data-shortcut="submit"]'
  );
  btn?.click();
}

export function useKeyboardShortcuts({
  tab,
  setTab,
  focusTab,
  helpOpen,
  openHelp,
  closeHelp,
  keytipsActive,
  showKeytips,
  hideKeytips,
}: ShortcutHandlers) {
  useEffect(() => {
    function onKeyDown(e: KeyboardEvent) {
      if (e.isComposing) return;
      if (e.defaultPrevented) return;

      // Avoid competing with browser/OS shortcuts.
      if (e.altKey) return;

      const editable = isEditableTarget(e.target);

      // Dismiss help
      if (helpOpen && e.key === "Escape") {
        e.preventDefault();
        closeHelp();
        return;
      }

      if (helpOpen) return;

      // Hold ? to show keytips (avoid when typing).
      if (!editable && !e.ctrlKey && !e.metaKey && e.key === "?") {
        e.preventDefault();
        if (!keytipsActive) showKeytips();
        return;
      }

      // Toggle shortcuts help (Ctrl/Cmd+/)
      if (!editable && (e.ctrlKey || e.metaKey) && e.key === "/") {
        e.preventDefault();
        if (helpOpen) closeHelp();
        else openHelp();
        return;
      }

      // Global tab switching (avoid when typing)
      if (!editable && !e.ctrlKey && !e.metaKey && !e.shiftKey) {
        if (e.key === "1") {
          e.preventDefault();
          setTab("split");
          focusTab("split");
          return;
        }
        if (e.key === "2") {
          e.preventDefault();
          setTab("combine");
          focusTab("combine");
          return;
        }
      }

      // Submit form
      if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
        e.preventDefault();
        clickSubmit(tab);
        return;
      }

      // Copy the recovered Secret. Split Recovery shares are copied individually.
      if (
        tab === "combine" &&
        (e.ctrlKey || e.metaKey) &&
        e.shiftKey &&
        (e.key === "C" || e.key === "c")
      ) {
        const copyButton = document.querySelector<HTMLButtonElement>(
          '#combine-panel button[data-shortcut="copy-result"]',
        );
        if (!copyButton) return;
        e.preventDefault();
        copyButton.click();
      }
    }

    function onKeyUp(e: KeyboardEvent) {
      if (e.isComposing) return;
      if (e.defaultPrevented) return;
      if (e.key === "?" && keytipsActive) {
        hideKeytips();
      }
    }

    function onWindowBlur() {
      if (keytipsActive) hideKeytips();
    }

    window.addEventListener("keydown", onKeyDown);

    window.addEventListener("keyup", onKeyUp);
    window.addEventListener("blur", onWindowBlur);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
      window.removeEventListener("blur", onWindowBlur);
    };
  }, [
    closeHelp,
    focusTab,
    helpOpen,
    hideKeytips,
    keytipsActive,
    openHelp,
    setTab,
    showKeytips,
    tab,
  ]);
}
