import { useCallback, useEffect, useRef, useState } from "react";

import { useAnnouncement } from "../context/LiveRegionContext";
import type { Lang, Strings } from "../i18n";
import { ensureWasm, recoveredSecretText } from "../wasm";

import { ClearButton } from "./ClearButton";
import { CopyButton } from "./CopyButton";
import { PasteButton } from "./PasteButton";
import { PassphraseInput } from "./PassphraseInput";
import {
  EncodingSelector,
  type Encoding,
  type EncodingOption,
} from "./ui/encoding-selector";

type CombineFormProps = {
  lang: Lang;
  strings: Strings;
};

type ShareBox = {
  id: string;
  value: string;
};

function MinusIcon() {
  return (
    <svg viewBox="0 0 24 24" fill="none" className="h-4 w-4" aria-hidden="true">
      <path
        d="M7 12h10"
        stroke="currentColor"
        strokeWidth="2"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

function rawErrorMessage(err: unknown): string {
  return err instanceof Error ? err.message : String(err);
}

function notEnoughSharesMatch(message: string): RegExpExecArray | null {
  return /need at least k shares: need (\d+), got (\d+)/i.exec(message);
}

function toErrorMessage(err: unknown, strings: Strings): string {
  const message = err instanceof Error ? err.message : String(err);
  if (/wasm_pkg|safeparts_wasm|Cannot find module/i.test(message))
    return strings.errorWasmMissing;
  if (/^(invalid packet|encoding error):/i.test(message))
    return strings.errorInvalidShare;

  const notEnoughShares = notEnoughSharesMatch(message);
  if (notEnoughShares) {
    const k = Number(notEnoughShares[1]);
    const got = Number(notEnoughShares[2]);
    const missing = Math.max(0, k - got);
    if (missing === 1) return strings.errorNotEnoughSharesOne;
    return strings.errorNotEnoughSharesMany.replace("{missing}", String(missing));
  }

  return message;
}

function parseSharesFromBox(text: string): string[] {
  return text
    .split(/\n\s*\n/g)
    .map((s) => s.trim())
    .filter(Boolean);
}

function isSupportedEncoding(value: unknown): value is Encoding {
  return value === "base64url" || value === "mnemo-words";
}

function createId(): string {
  if (typeof crypto !== "undefined" && typeof crypto.randomUUID === "function")
    return crypto.randomUUID();
  return `${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function createShareBox(): ShareBox {
  return { id: createId(), value: "" };
}

export function CombineForm({ lang, strings }: CombineFormProps) {
  const { announce } = useAnnouncement();
  const [encoding, setEncoding] = useState<Encoding>("mnemo-words");
  const [passphrase, setPassphrase] = useState("");
  const [shareBoxes, setShareBoxes] = useState<ShareBox[]>(() => [
    createShareBox(),
    createShareBox(),
  ]);
  const [invalidShareBoxIds, setInvalidShareBoxIds] = useState<string[]>([]);
  const [shareBoxFlashIds, setShareBoxFlashIds] = useState<string[]>([]);
  const [secret, setSecret] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const [encodingFlash, setEncodingFlash] = useState(false);
  const pasteRequestedRef = useRef(false);
  const pendingShareBoxFlashIdsRef = useRef<string[] | null>(null);
  const flashTimeoutRef = useRef<number | null>(null);
  const shareBoxFlashTimeoutRef = useRef<number | null>(null);
  const shareTextareasRef = useRef<Record<string, HTMLTextAreaElement>>({});
  const recoveryInputVersionRef = useRef(0);

  const shareBoxCount = shareBoxes.length;

  const invalidateRecoveryResult = useCallback(() => {
    recoveryInputVersionRef.current += 1;
    setSecret("");
    setError(null);
    setBusy(false);
  }, []);

  const encodingOptions: EncodingOption[] = [
    {
      value: "mnemo-words",
      label: strings.encodingMnemoWords,
      description: strings.encodingMnemoWordsDesc,
    },
    {
      value: "base64url",
      label: strings.encodingBase64url,
      description: strings.encodingBase64urlDesc,
    },
  ];

  const triggerEncodingFlash = useCallback(() => {
    setEncodingFlash(true);
    if (flashTimeoutRef.current !== null) {
      window.clearTimeout(flashTimeoutRef.current);
    }
    flashTimeoutRef.current = window.setTimeout(() => {
      setEncodingFlash(false);
    }, 1100);
  }, []);

  const triggerShareBoxFlash = useCallback((ids: string[]) => {
    setShareBoxFlashIds(ids);
    if (shareBoxFlashTimeoutRef.current !== null) {
      window.clearTimeout(shareBoxFlashTimeoutRef.current);
    }
    shareBoxFlashTimeoutRef.current = window.setTimeout(() => {
      setShareBoxFlashIds([]);
    }, 1100);
  }, []);

  const resizeShareTextarea = useCallback((textarea: HTMLTextAreaElement) => {
    textarea.style.height = "auto";
    textarea.style.height = `${textarea.scrollHeight}px`;
  }, []);

  const setShareTextareaRef = useCallback(
    (id: string, textarea: HTMLTextAreaElement | null) => {
      if (!textarea) {
        delete shareTextareasRef.current[id];
        return;
      }

      shareTextareasRef.current[id] = textarea;
      resizeShareTextarea(textarea);
    },
    [resizeShareTextarea],
  );

  useEffect(() => {
    return () => {
      if (flashTimeoutRef.current !== null) {
        window.clearTimeout(flashTimeoutRef.current);
      }
      if (shareBoxFlashTimeoutRef.current !== null) {
        window.clearTimeout(shareBoxFlashTimeoutRef.current);
      }
    };
  }, []);

  useEffect(() => {
    const ids = pendingShareBoxFlashIdsRef.current;
    if (!ids || ids.length === 0) return;
    if (shareBoxCount < 2) return;
    pendingShareBoxFlashIdsRef.current = null;
    triggerShareBoxFlash(ids);
  }, [shareBoxCount, triggerShareBoxFlash]);

  useEffect(() => {
    for (const box of shareBoxes) {
      const textarea = shareTextareasRef.current[box.id];
      if (textarea) resizeShareTextarea(textarea);
    }
  }, [shareBoxes, resizeShareTextarea]);

  useEffect(() => {
    if (!pasteRequestedRef.current) return;
    pasteRequestedRef.current = false;

    const firstNonEmpty = shareBoxes.find((b) => b.value.trim().length > 0);
    if (!firstNonEmpty) return;

    const firstShare = parseSharesFromBox(firstNonEmpty.value)[0];
    if (!firstShare) return;

    const inspectionVersion = recoveryInputVersionRef.current;

    (async () => {
      try {
        const wasm = await ensureWasm();

        const info =
          typeof wasm.inspect_share_input === "function"
            ? await wasm.inspect_share_input(firstNonEmpty.value, "auto")
            : typeof wasm.inspect_share === "function"
              ? await wasm.inspect_share(firstShare, encoding)
              : typeof wasm.share_threshold === "function"
                ? { k: await wasm.share_threshold(firstShare, encoding), encoding }
                : null;

        if (inspectionVersion !== recoveryInputVersionRef.current) return;

        const detected = info?.encoding;
        if (isSupportedEncoding(detected) && detected !== encoding) {
          setEncoding(detected);
          triggerEncodingFlash();
        }

        const k = Number(info?.k);
        if (!Number.isFinite(k) || k < 2) return;

        setShareBoxes((prev) => {
          if (prev.length >= k) return prev;

          const toAdd = k - prev.length;
          const added = Array.from({ length: toAdd }, () => createShareBox());
          pendingShareBoxFlashIdsRef.current = added.map((b) => b.id);
          return [...prev, ...added];
        });
      } catch {
        // Ignore: auto-expanding share slots is a best-effort UX enhancement.
      }
    })();
  }, [shareBoxes, encoding, triggerEncodingFlash]);

  const shares = shareBoxes.flatMap((box) => parseSharesFromBox(box.value));
  const combinedShareInput = shareBoxes
    .map((box) => box.value.trim())
    .filter(Boolean)
    .join("\n\n");

  function setShareBoxValue(id: string, value: string) {
    invalidateRecoveryResult();
    setShareBoxes((prev) =>
      prev.map((b) => (b.id === id ? { ...b, value } : b)),
    );

    if (invalidShareBoxIds.includes(id) && value.trim().length > 0) {
      setInvalidShareBoxIds((prev) => prev.filter((v) => v !== id));
    }
  }

  function addShareBox() {
    invalidateRecoveryResult();
    setShareBoxes((prev) => [...prev, createShareBox()]);
  }

  function removeShareBox(id: string) {
    if (shareBoxes.length <= 2) return;
    invalidateRecoveryResult();
    setShareBoxes((prev) => prev.filter((b) => b.id !== id));
    setInvalidShareBoxIds((prev) => prev.filter((v) => v !== id));
  }

  function updatePassphrase(value: string) {
    if (value === passphrase) return;

    invalidateRecoveryResult();
    setPassphrase(value);
  }

  async function onCombine() {
    const recoveryVersion = recoveryInputVersionRef.current;
    setBusy(true);
    setError(null);
    setSecret("");

    try {
      const wasm = await ensureWasm();
      if (recoveryVersion !== recoveryInputVersionRef.current) return;

      const out =
        typeof wasm.combine_share_input === "function"
          ? await wasm.combine_share_input(
              combinedShareInput,
              encoding,
              passphrase ? passphrase : undefined,
            )
          : await wasm.combine_shares(
              shares,
              encoding,
              passphrase ? passphrase : undefined,
            );
      if (recoveryVersion !== recoveryInputVersionRef.current) return;

      const recoveredText = recoveredSecretText(out);
      setInvalidShareBoxIds([]);
      if (recoveredText === null) {
        setError(strings.errorRecoveredSecretNotText);
        return;
      }

      setSecret(recoveredText);
      announce(strings.recoverySuccess, "polite");
    } catch (e) {
      if (recoveryVersion !== recoveryInputVersionRef.current) return;

      const rawMessage = rawErrorMessage(e);
      const message = toErrorMessage(e, strings);
      setError(message);

      const m = notEnoughSharesMatch(rawMessage);
      if (m) {
        const k = Number(m[1]);
        const got = Number(m[2]);
        const missing = Math.max(0, k - got);

        if (missing > 0) {
          const emptyBoxIds = shareBoxes
            .filter((b) => parseSharesFromBox(b.value).length === 0)
            .map((b) => b.id);
          setInvalidShareBoxIds(emptyBoxIds.slice(0, missing));
        }
      }
    } finally {
      if (recoveryVersion === recoveryInputVersionRef.current) {
        setBusy(false);
      }
    }
  }

  const canCombine = shares.length > 0;

  return (
    <section className="glass p-4 sm:p-6">
      <div className="dir-row items-start justify-between gap-4">
        <div className="text-start">
          <h2 className="text-lg font-semibold">{strings.combineTitle}</h2>
          <p className="mt-1 text-sm text-slate-300">
            {strings.combineSubtitle}
          </p>
        </div>
      </div>

      <div className="mt-6 grid grid-cols-1 gap-4">
        <div className="block text-start">
          <span className="field-label" id="encoding-label">{strings.encodingLabel}</span>
          <EncodingSelector
            value={encoding}
            onChange={(value) => {
              invalidateRecoveryResult();
              setEncoding(value);
            }}
            options={encodingOptions}
            flash={encodingFlash}
          />
        </div>

        <div className="block text-start">
          <label
            className="field-label"
            id="recover-passphrase-label"
            htmlFor="recover-passphrase"
          >
            {strings.passphraseLabel}
          </label>
          <p className="field-hint mt-1" id="recover-passphrase-hint">
            {strings.passphraseHint}
          </p>
          <PassphraseInput
            id="recover-passphrase"
            value={passphrase}
            onChange={updatePassphrase}
            autoComplete="current-password"
            labelledBy="recover-passphrase-label"
            describedBy="recover-passphrase-hint"
            showLabel={strings.showPassphrase}
            hideLabel={strings.hidePassphrase}
            pasteLabel={strings.pastePassphrase}
            clearLabel={strings.clearPassphrase}
          />
        </div>

        <div>
          <div className="dir-row items-start justify-between gap-3">
            <div className="text-start">
              <div className="field-label" id="shares-label">{strings.sharesInputLabel}</div>
              <div className="field-hint mt-1" id="shares-hint">{strings.sharesInputHint}</div>
            </div>
          </div>

          <div className="mt-3 divide-y divide-emerald-500/10">
            {shareBoxes.map((box, i) => {
              const isInvalid = invalidShareBoxIds.includes(box.id);
              const isFlashing = shareBoxFlashIds.includes(box.id);

              return (
                <div key={box.id} className="py-4 first:pt-0 last:pb-0">
                  <div className="dir-row items-center justify-between gap-3">
                    <div className="text-start text-xs font-semibold text-slate-200">
                      {strings.shareNumber} {i + 1}
                    </div>

                    <button
                      type="button"
                      className="btn-ghost h-11 w-11 min-h-[44px] px-0 text-slate-400 transition-colors hover:bg-transparent hover:text-slate-200 focus-visible:text-slate-100 disabled:opacity-25 disabled:hover:text-slate-400"
                      onClick={() => removeShareBox(box.id)}
                      disabled={shareBoxes.length <= 2}
                      aria-label={strings.removeShare}
                      title={strings.removeShare}
                    >
                      <MinusIcon />
                      <span className="sr-only">{strings.removeShare}</span>
                    </button>
                  </div>

                  <div className="relative mt-2" dir="ltr">
                    <textarea
                      ref={(textarea) => setShareTextareaRef(box.id, textarea)}
                      dir="ltr"
                      value={box.value}
                      onChange={(e) => {
                        setShareBoxValue(box.id, e.target.value);
                        resizeShareTextarea(e.currentTarget);
                      }}
                      onPaste={() => {
                        pasteRequestedRef.current = true;
                      }}
                      rows={3}
                      className={`input input-with-clear input-with-clear-ltr resize-none overflow-hidden font-mono text-xs leading-relaxous transition-colors duration-700 ${
                        isInvalid
                          ? "border-rose-400 focus:border-rose-400 focus:ring-rose-500/15"
                          : ""
                      } ${isFlashing ? "border-emerald-300/70 bg-emerald-500/15" : ""}`}
                      aria-labelledby="shares-label"
                      aria-describedby={
                        isInvalid ? `share-${box.id}-error` : "shares-hint"
                      }
                      aria-invalid={isInvalid}
                    />
                    {box.value.length === 0 ? (
                      <span
                        aria-hidden="true"
                        dir={lang === "ar" ? "rtl" : "ltr"}
                        className={`pointer-events-none absolute inset-x-3 top-2 overflow-hidden pe-12 text-sm text-slate-400 ${
                          lang === "ar" ? "text-right" : "text-left"
                        }`}
                      >
                        {strings.sharePlaceholder}
                      </span>
                    ) : null}
                    {box.value.length === 0 ? (
                      <PasteButton
                        label={`${strings.pasteShare} ${i + 1}`}
                        onPaste={(text) => {
                          pasteRequestedRef.current = true;
                          setShareBoxValue(box.id, text);
                        }}
                        className="absolute top-2 end-2"
                      />
                    ) : (
                      <ClearButton
                        label={`${strings.clearShare} ${i + 1}`}
                        onClick={() => setShareBoxValue(box.id, "")}
                        className="absolute top-2 end-2"
                      />
                    )}
                  </div>
                  {isInvalid && (
                    <p className="mt-1 text-xs text-rose-400" id={`share-${box.id}-error`} role="alert">
                      {strings.shareRequired}
                    </p>
                  )}
                </div>
              );
            })}
          </div>

          <div className="dir-row mt-3 justify-start">
            <button
              type="button"
              className="btn-secondary w-full px-3 py-2 text-xs sm:w-auto"
              onClick={addShareBox}
            >
              {strings.addShare}
            </button>
          </div>
        </div>

        <div className="dir-row flex-col items-stretch gap-3 sm:flex-row sm:items-center sm:justify-between">
          <button
            type="button"
            data-shortcut="submit"
            data-keytip="Ctrl/Cmd+Enter"
            disabled={!canCombine || busy}
            onClick={onCombine}
            className="btn-primary w-full sm:w-auto"
          >
            {busy ? strings.working : strings.combineCta}
          </button>
        </div>

        {error ? (
          <div className="alert-error" role="alert" aria-live="assertive">
            {error}
          </div>
        ) : null}
      </div>

      {secret ? (
        <div className="mt-6 rounded-2xl border border-emerald-500/15 bg-black/35 p-3">
          <div className="dir-row items-start justify-between gap-3">
            <div className="text-start">
              <h3 className="text-sm font-semibold text-slate-200">
                {strings.recoveredTitle}
              </h3>
              <p className="mt-1 text-xs text-slate-400">
                {strings.recoveredHint}
              </p>
            </div>

            <CopyButton
              value={secret}
              shortcut="copy-result"
              announceCopied={strings.copied}
              copyLabel={strings.copy}
              copiedLabel={strings.copied}
              copyFailedLabel={strings.copyFailed}
              accessibleLabel={strings.copyRecovered}
              className="shrink-0"
            />
          </div>

          <div
            dir="auto"
            className="input mt-3 min-h-[120px] resize-y font-mono text-xs text-slate-200 leading-relaxed whitespace-pre-wrap break-words"
          >
            {secret}
          </div>
        </div>
      ) : null}
    </section>
  );
}
