import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type FocusEvent,
  type MouseEvent as ReactMouseEvent,
} from "react";

import type { Strings } from "../i18n";
import { ensureWasm } from "../wasm";

import { ClearButton } from "./ClearButton";
import { CopyButton } from "./CopyButton";
import { PasteButton } from "./PasteButton";
import { PassphraseInput } from "./PassphraseInput";
import {
  EncodingSelector,
  type Encoding,
  type EncodingOption,
} from "./ui/encoding-selector";

type SplitFormProps = {
  strings: Strings;
};

function toErrorMessage(err: unknown, strings: Strings): string {
  const message = err instanceof Error ? err.message : String(err);
  if (/wasm_pkg|safeparts_wasm|Cannot find module/i.test(message))
    return strings.errorWasmMissing;
  return message;
}

function resizeTextarea(textarea: HTMLTextAreaElement) {
  textarea.style.height = "auto";
  textarea.style.height = `${textarea.scrollHeight}px`;
}

export function SplitForm({ strings }: SplitFormProps) {
  const secretTextareaRef = useRef<HTMLTextAreaElement | null>(null);
  const splitOperationRef = useRef(0);
  const [secret, setSecret] = useState("");
  const [k, setK] = useState(2);
  const [n, setN] = useState(3);
  const [isCoarsePointer, setIsCoarsePointer] = useState(() => {
    const media = typeof window !== "undefined" ? window.matchMedia?.("(pointer: coarse)") : null;
    return media?.matches ?? false;
  });

  useEffect(() => {
    const media = typeof window !== "undefined" ? window.matchMedia?.("(pointer: coarse)") : null;
    if (!media) return;

    const update = () => setIsCoarsePointer(media.matches);
    update();

    if (typeof media.addEventListener === "function") {
      media.addEventListener("change", update);
      return () => media.removeEventListener("change", update);
    }

    // Safari < 14
    media.addListener(update);
    return () => media.removeListener(update);
  }, []);

  const setSecretTextareaRef = useCallback((textarea: HTMLTextAreaElement | null) => {
    secretTextareaRef.current = textarea;
    if (textarea) resizeTextarea(textarea);
  }, []);

  function clampK(nextK: number, nextN: number): number {
    if (!Number.isFinite(nextK)) return 2;
    if (!Number.isFinite(nextN)) return 2;

    const safeN = Math.min(255, Math.max(2, Math.floor(nextN)));
    const safeK = Math.floor(nextK);
    return Math.min(safeN, Math.max(2, safeK));
  }

  function clampN(nextN: number): number {
    if (!Number.isFinite(nextN)) return 2;
    return Math.min(255, Math.max(2, Math.floor(nextN)));
  }

  function selectAllOnFocus(e: FocusEvent<HTMLInputElement>) {
    if (!isCoarsePointer) return;
    // Makes it easy to replace the value on mobile.
    const input = e.currentTarget;
    requestAnimationFrame(() => {
      if (input.isConnected && input.ownerDocument.activeElement === input) {
        input.select();
      }
    });
  }

  function selectAllOnClick(e: ReactMouseEvent<HTMLInputElement>) {
    if (!isCoarsePointer) return;
    e.currentTarget.select();
  }
  const [encoding, setEncoding] = useState<Encoding>("mnemo-words");
  const [passphrase, setPassphrase] = useState("");
  const [passphraseConfirmation, setPassphraseConfirmation] = useState("");
  const [shares, setShares] = useState<string[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

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

  const passphrasesMatch =
    passphrase.length === 0 || passphrase === passphraseConfirmation;
  const canSplit =
    secret.length > 0 && k >= 2 && n >= 2 && n <= 255 && passphrasesMatch;

  function invalidateGeneratedShares() {
    splitOperationRef.current += 1;
    setShares([]);
    setError(null);
    setBusy(false);
  }

  function updateSecret(value: string) {
    if (value === secret) return;
    invalidateGeneratedShares();
    setSecret(value);
  }

  function updateK(value: number) {
    const safeK = clampK(value, n);
    if (safeK === k) return;
    invalidateGeneratedShares();
    setK(safeK);
  }

  function updateN(value: number) {
    const safeN = clampN(value);
    if (safeN === n) return;
    invalidateGeneratedShares();
    setN(safeN);
    setK((previousK) => clampK(previousK, safeN));
  }

  function updateEncoding(value: Encoding) {
    if (value === encoding) return;
    invalidateGeneratedShares();
    setEncoding(value);
  }

  function updatePassphrase(value: string) {
    if (value === passphrase) return;
    invalidateGeneratedShares();
    setPassphrase(value);
    if (value.length === 0) setPassphraseConfirmation("");
  }

  function updatePassphraseConfirmation(value: string) {
    if (value === passphraseConfirmation) return;
    invalidateGeneratedShares();
    setPassphraseConfirmation(value);
  }

  async function onSplit() {
    if (!passphrasesMatch) {
      setError(strings.passphraseMismatch);
      return;
    }

    const operationId = splitOperationRef.current + 1;
    splitOperationRef.current = operationId;
    setBusy(true);
    setError(null);
    setShares([]);

    try {
      const wasm = await ensureWasm();
      const bytes = new TextEncoder().encode(secret);
      const out = await wasm.split_secret(
        bytes,
        k,
        n,
        encoding,
        passphrase ? passphrase : undefined,
      );
      if (operationId !== splitOperationRef.current) return;

      const outShares = Array.from(out).map((v) => String(v));
      setShares(outShares);
    } catch (e) {
      if (operationId === splitOperationRef.current) {
        setError(toErrorMessage(e, strings));
      }
    } finally {
      if (operationId === splitOperationRef.current) {
        setBusy(false);
      }
    }
  }

  return (
    <section className="glass p-4 sm:p-6">
      <div className="dir-row items-start justify-between gap-4">
        <div>
          <h2 className="text-lg font-semibold">{strings.splitTitle}</h2>
          <p className="mt-1 text-sm text-slate-300">{strings.splitSubtitle}</p>
        </div>
      </div>

      <div className="mt-6 grid grid-cols-1 gap-4">
        <label className="block">
          <span className="field-label" id="secret-label">{strings.secretLabel}</span>
          <span className="field-hint mt-1 block" id="secret-hint">{strings.secretHint}</span>
          <div className="relative mt-3">
            <textarea
              ref={setSecretTextareaRef}
              autoCorrect="off"
              autoCapitalize="none"
              spellCheck={false}
              value={secret}
              onChange={(e) => {
                updateSecret(e.target.value);
                resizeTextarea(e.currentTarget);
              }}
              rows={4}
              className="input input-with-clear min-h-[120px] resize-none overflow-hidden font-mono text-xs leading-relaxed"
              aria-labelledby="secret-label"
              aria-describedby="secret-hint"
            />
            {secret.length === 0 ? (
              <PasteButton
                label={strings.pasteSecret}
                onPaste={(text) => {
                  updateSecret(text);
                  requestAnimationFrame(() => {
                    if (secretTextareaRef.current) {
                      resizeTextarea(secretTextareaRef.current);
                    }
                  });
                }}
                className="absolute top-2 end-2"
              />
            ) : (
              <ClearButton
                label={strings.clearSecret}
                onClick={() => {
                  updateSecret("");
                  requestAnimationFrame(() => {
                    if (secretTextareaRef.current) {
                      resizeTextarea(secretTextareaRef.current);
                    }
                  });
                }}
                className="absolute top-2 end-2"
              />
            )}
          </div>
        </label>

        <div className="grid grid-cols-1 gap-3 sm:grid-cols-3">
          <label className="block" htmlFor="split-k">
            <span className="field-label block sm:min-h-10" id="k-label">
              {strings.kLabel}
            </span>
            {isCoarsePointer ? (
              <div className="input-stepper-shell mt-2">
                <input
                  id="split-k"
                  type="number"
                  inputMode="numeric"
                  min={2}
                  max={Math.min(255, n)}
                  value={k}
                  onChange={(e) => updateK(Number(e.target.value))}
                  onFocus={selectAllOnFocus}
                  onClick={selectAllOnClick}
                  className="input-stepper-field"
                  aria-labelledby="k-label"
                />
                <div className="stepper-controls items-center gap-1 p-1">
                  <button
                    type="button"
                    className="grid h-10 w-10 place-items-center rounded-lg border border-emerald-500/15 bg-black/35 text-sm text-slate-200 hover:bg-white/5 disabled:cursor-not-allowed disabled:opacity-50"
                    onClick={() => updateK(k - 1)}
                    disabled={k <= 2}
                    aria-label={strings.decrement}
                    title={strings.decrement}
                  >
                    −
                  </button>
                  <button
                    type="button"
                    className="grid h-10 w-10 place-items-center rounded-lg border border-emerald-500/15 bg-black/35 text-sm text-slate-200 hover:bg-white/5 disabled:cursor-not-allowed disabled:opacity-50"
                    onClick={() => updateK(k + 1)}
                    disabled={k >= Math.min(255, n)}
                    aria-label={strings.increment}
                    title={strings.increment}
                  >
                    +
                  </button>
                </div>
              </div>
            ) : (
              <input
                id="split-k"
                type="number"
                min={2}
                max={Math.min(255, n)}
                value={k}
                onChange={(e) => updateK(Number(e.target.value))}
                className="input mt-2"
                aria-labelledby="k-label"
              />
            )}
          </label>

          <label className="block" htmlFor="split-n">
            <span className="field-label block sm:min-h-10" id="n-label">
              {strings.nLabel}
            </span>
            {isCoarsePointer ? (
              <div className="input-stepper-shell mt-2">
                <input
                  id="split-n"
                  type="number"
                  inputMode="numeric"
                  min={2}
                  max={255}
                  value={n}
                  onChange={(e) => updateN(Number(e.target.value))}
                  onFocus={selectAllOnFocus}
                  onClick={selectAllOnClick}
                  className="input-stepper-field"
                  aria-labelledby="n-label"
                />
                <div className="stepper-controls items-center gap-1 p-1">
                  <button
                    type="button"
                    className="grid h-10 w-10 place-items-center rounded-lg border border-emerald-500/15 bg-black/35 text-sm text-slate-200 hover:bg-white/5 disabled:cursor-not-allowed disabled:opacity-50"
                    onClick={() => updateN(n - 1)}
                    disabled={n <= 2}
                    aria-label={strings.decrement}
                    title={strings.decrement}
                  >
                    −
                  </button>
                  <button
                    type="button"
                    className="grid h-10 w-10 place-items-center rounded-lg border border-emerald-500/15 bg-black/35 text-sm text-slate-200 hover:bg-white/5 disabled:cursor-not-allowed disabled:opacity-50"
                    onClick={() => updateN(n + 1)}
                    disabled={n >= 255}
                    aria-label={strings.increment}
                    title={strings.increment}
                  >
                    +
                  </button>
                </div>
              </div>
            ) : (
              <input
                id="split-n"
                type="number"
                min={2}
                max={255}
                value={n}
                onChange={(e) => updateN(Number(e.target.value))}
                className="input mt-2"
                aria-labelledby="n-label"
              />
            )}
          </label>

          <div className="block sm:col-span-3">
            <span className="field-label block" id="encoding-label">
              {strings.encodingLabel}
            </span>
            <EncodingSelector value={encoding} onChange={updateEncoding} options={encodingOptions} />
          </div>
        </div>

        <div className="block">
          <label className="field-label" id="split-passphrase-label" htmlFor="split-passphrase">
            {strings.passphraseLabel}
          </label>
          <p className="field-hint mt-1" id="split-passphrase-hint">
            {strings.passphraseHint}
          </p>
          <PassphraseInput
            id="split-passphrase"
            value={passphrase}
            onChange={updatePassphrase}
            autoComplete="new-password"
            labelledBy="split-passphrase-label"
            describedBy="split-passphrase-hint"
            showLabel={strings.showPassphrase}
            hideLabel={strings.hidePassphrase}
            pasteLabel={strings.pastePassphrase}
            clearLabel={strings.clearPassphrase}
          />
        </div>

        {passphrase.length > 0 ? (
          <div className="block">
            <label
              className="field-label"
              id="split-passphrase-confirmation-label"
              htmlFor="split-passphrase-confirmation"
            >
              {strings.confirmPassphraseLabel}
            </label>
            <PassphraseInput
              id="split-passphrase-confirmation"
              value={passphraseConfirmation}
              onChange={updatePassphraseConfirmation}
              autoComplete="new-password"
              labelledBy="split-passphrase-confirmation-label"
              describedBy={
                passphrasesMatch
                  ? "split-passphrase-hint"
                  : "split-passphrase-mismatch"
              }
              showLabel={strings.showPassphraseConfirmation}
              hideLabel={strings.hidePassphraseConfirmation}
              pasteLabel={strings.pastePassphraseConfirmation}
              clearLabel={strings.clearPassphraseConfirmation}
              invalid={!passphrasesMatch}
            />
            {!passphrasesMatch ? (
              <p
                className="mt-1 text-xs text-rose-400"
                id="split-passphrase-mismatch"
                role="alert"
              >
                {strings.passphraseMismatch}
              </p>
            ) : null}
          </div>
        ) : null}

        <div className="dir-row flex-col items-stretch gap-3 sm:flex-row sm:items-center sm:justify-between">
          <button
            type="button"
            data-shortcut="submit"
            data-keytip="Ctrl/Cmd+Enter"
            disabled={!canSplit || busy}
            onClick={onSplit}
            className="btn-primary w-full sm:w-auto"
          >
            {busy ? strings.working : strings.splitCta}
          </button>
        </div>

        {error ? (
          <div className="alert-error" role="alert" aria-live="assertive">
            {error}
          </div>
        ) : null}
      </div>

      {shares.length > 0 ? (
        <div className="mt-6">
          <div role="status" className="sr-only">
            {strings.recoverySharesReady.replace("{count}", String(shares.length))}
          </div>
          <div className="dir-row items-start justify-between gap-3">
            <div>
              <h3 className="text-sm font-semibold text-slate-200">
                {strings.sharesTitle}
              </h3>
              <p className="mt-1 text-xs text-slate-400">
                {strings.sharesHint}
              </p>
            </div>
          </div>

          <div className="mt-3 divide-y divide-emerald-500/10">
            {shares.map((s, i) => (
              <div
                key={`${i}-${s.slice(0, 16)}`}
                className="py-4 first:pt-0 last:pb-0"
              >
                <div className="dir-row items-center justify-between gap-3">
                  <div className="text-xs font-semibold text-slate-200">
                    {strings.shareNumber} {i + 1}
                  </div>
                  <CopyButton
                    value={s}
                    copyLabel={strings.copy}
                    copiedLabel={strings.copied}
                    copyFailedLabel={strings.copyFailed}
                    accessibleLabel={strings.copyShare.replace("{number}", String(i + 1))}
                  />
                </div>

                <div
                  dir="ltr"
                  className="input mt-2 font-mono text-xs text-slate-200 leading-relaxed whitespace-pre-wrap break-words"
                >
                  {s}
                </div>
              </div>
            ))}
          </div>
        </div>
      ) : null}
    </section>
  );
}
