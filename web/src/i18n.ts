export type Lang = "en" | "ar";

export const STRINGS = {
  en: {
    appName: "Safeparts",
    tagline: "Recover critical secrets without keeping them in one place.",

    splitTab: "Split",
    combineTab: "Combine",

    theme: "Theme",
    dark: "Dark",
    light: "Light",

    language: "Language",
    english: "English",
    arabic: "العربية",
    github: "GitHub",
    discord: "Discord",
    help: "Docs",
    changelog: "Changelog",
    privacyNote:
      "Runs locally in your browser. Your secret never leaves your device.",

    splitTitle: "Split",
    splitSubtitle: "Turn one secret into multiple shares.",
    combineTitle: "Combine",
    combineSubtitle: "Paste shares to recover the secret.",

    secretLabel: "Secret",
    secretHint: "Anything: password, seed phrase, JSON…",

    kLabel: "Minimum shares to recover (k)",
    nLabel: "Total shares to create (n)",
    encodingLabel: "Share format",
    encodingBase64url: "Letters",
    encodingBase64urlDesc: "Compact alphanumeric",
    encodingMnemoWords: "Words",
    encodingMnemoWordsDesc: "Easy to write mnemonic words",
    passphraseLabel: "Passphrase (optional)",
    passphraseHint:
      "Passphrase protection requires this exact value for recovery. Safeparts cannot reset it.",
    confirmPassphraseLabel: "Confirm passphrase",
    passphraseMismatch: "Passphrases must match exactly.",
    showPassphrase: "Show passphrase",
    hidePassphrase: "Hide passphrase",
    showPassphraseConfirmation: "Show passphrase confirmation",
    hidePassphraseConfirmation: "Hide passphrase confirmation",

    clearSecret: "Clear secret",
    clearPassphrase: "Clear passphrase",
    clearPassphraseConfirmation: "Clear passphrase confirmation",
    clearShare: "Clear share",
    pasteSecret: "Paste secret",
    pastePassphrase: "Paste passphrase",
    pastePassphraseConfirmation: "Paste passphrase confirmation",
    pasteShare: "Paste share",

    splitCta: "Split",
    combineCta: "Combine",
    working: "Working…",

    sharesTitle: "Recovery shares",
    sharesHint:
      "Keep fewer Recovery shares than the Threshold in every account, device, location, administrator domain, and transport channel.",
    shareNumber: "Recovery share",
    recoverySharesReady: "{count} Recovery shares ready.",

    sharesInputLabel: "Shares",
    sharesInputHint: "One share per box. You can add more as needed.",
    sharePlaceholder: "Paste a share here…",
    shareRequired: "Share content is required",
    addShare: "Add share",
    removeShare: "Remove",

    recoveredTitle: "Recovered secret",
    recoveredHint: "Handle carefully — this is sensitive.",
    recoverySuccess: "Secret recovered.",

    copy: "Copy",
    copied: "Copied",
    copyFailed: "Could not copy to the clipboard.",
    copyShare: "Copy Recovery share {number}",
    copyRecovered: "Copy recovered Secret",
    increment: "Increase",
    decrement: "Decrease",

    wasmHint: "Before using this UI, run",
    wasmCommand: "bun run build:wasm",

    errorWasmMissing: "WASM module not found. Run bun run build:wasm.",
    errorInvalidShare: "Invalid share",
    errorRecoveredSecretNotText:
      "This recovered Secret is not valid UTF-8 text. Use the CLI, TUI, or a native file workflow to recover the exact bytes.",
    errorNotEnoughSharesOne: "Add 1 more share to recover this secret.",
    errorNotEnoughSharesMany: "Add {missing} more shares to recover this secret.",

    keyboardShortcuts: "Keyboard shortcuts",
    shortcutClose: "Close",
    shortcutGoToSplit: "Go to Split",
    shortcutGoToCombine: "Go to Combine",
    shortcutSubmitForm: "Split/Combine",
    shortcutCopyResult: "Copy recovered Secret (Combine only)",
    shortcutShowHelp: "Show shortcuts",
    shortcutShowKeytips: "Show keytips overlay",
  },
  ar: {
    appName: "Safeparts",
    tagline: "استعد أسرارك المهمة دون إبقائها في مكان واحد.",

    splitTab: "تقسيم",
    combineTab: "استعادة",

    theme: "المظهر",
    dark: "داكن",
    light: "فاتح",

    language: "اللغة",
    english: "English",
    arabic: "العربية",
    github: "GitHub",
    discord: "ديسكورد",
    help: "مساعدة",
    changelog: "سجل التغييرات",
    privacyNote: "يعمل محليا في المتصفح. الأسرار لا تغادر جهازك.",

    splitTitle: "تقسيم",
    splitSubtitle: "حول سرا واحدا إلى عدة حصص.",
    combineTitle: "استعادة",
    combineSubtitle: "الصق الحصص لاستعادة السر.",

    secretLabel: "السر",
    secretHint: "أي شيء: كلمة مرور، seed phrase، JSON…",

    kLabel: "الحد الأدنى للاستعادة (k)",
    nLabel: "إجمالي الحصص (n)",
    encodingLabel: "صيغة الحصة",
    encodingBase64url: "أحرف",
    encodingBase64urlDesc: "أحرف وأرقام مضغوطة",
    encodingMnemoWords: "كلمات",
    encodingMnemoWordsDesc: "كلمات سهلة الكتابة",
    passphraseLabel: "عبارة مرور (اختياري)",
    passphraseHint:
      "تتطلب حماية عبارة المرور هذه القيمة نفسها للاسترداد. لا يمكن لـ Safeparts إعادة تعيينها.",
    confirmPassphraseLabel: "تأكيد عبارة المرور",
    passphraseMismatch: "يجب أن تتطابق عبارتا المرور تماماً.",
    showPassphrase: "إظهار عبارة المرور",
    hidePassphrase: "إخفاء عبارة المرور",
    showPassphraseConfirmation: "إظهار تأكيد عبارة المرور",
    hidePassphraseConfirmation: "إخفاء تأكيد عبارة المرور",

    clearSecret: "مسح السر",
    clearPassphrase: "مسح عبارة المرور",
    clearPassphraseConfirmation: "مسح تأكيد عبارة المرور",
    clearShare: "مسح الحصة",
    pasteSecret: "لصق السر",
    pastePassphrase: "لصق عبارة المرور",
    pastePassphraseConfirmation: "لصق تأكيد عبارة المرور",
    pasteShare: "لصق الحصة",

    splitCta: "قسم",
    combineCta: "استعادة",
    working: "جار العمل…",

    sharesTitle: "حصص الاسترداد",
    sharesHint:
      "احتفظ بعدد من حصص الاسترداد أقل من العتبة في كل حساب وجهاز وموقع ونطاق إدارة وقناة نقل.",
    shareNumber: "حصة استرداد",
    recoverySharesReady: "{count} حصص استرداد جاهزة.",

    sharesInputLabel: "الحصص",
    sharesInputHint: "حصة واحدة في كل مربع. أضف المزيد عند الحاجة.",
    sharePlaceholder: "الصق الحصة هنا…",
    shareRequired: "محتوى الحصة مطلوب",
    addShare: "إضافة حصة",
    removeShare: "حذف",

    recoveredTitle: "السر المستعاد",
    recoveredHint: "تعامل بحذر — هذه بيانات حساسة.",
    recoverySuccess: "تمت استعادة السر.",

    copy: "نسخ",
    copied: "تم النسخ",
    copyFailed: "تعذر النسخ إلى الحافظة.",
    copyShare: "نسخ حصة الاسترداد {number}",
    copyRecovered: "نسخ السر المستعاد",
    increment: "زيادة",
    decrement: "تقليل",

    wasmHint: "قبل استخدام الواجهة، شغل",
    wasmCommand: "bun run build:wasm",

    errorWasmMissing: "لم يتم العثور على WASM. شغل bun run build:wasm.",
    errorInvalidShare: "حصة غير صالحة",
    errorRecoveredSecretNotText:
      "السر المستعاد ليس نصًا صالحًا بترميز UTF-8. استخدم CLI أو TUI أو سير عمل ملفات أصليًا لاستعادة البايتات بدقة.",
    errorNotEnoughSharesOne: "أضف حصة واحدة أخرى لاستعادة هذا السر.",
    errorNotEnoughSharesMany: "أضف {missing} حصص أخرى لاستعادة هذا السر.",

    keyboardShortcuts: "اختصارات لوحة المفاتيح",
    shortcutClose: "إغلاق",
    shortcutGoToSplit: "الذهاب للتقسيم",
    shortcutGoToCombine: "الذهاب للاستعادة",
    shortcutSubmitForm: "إرسال النموذج",
    shortcutCopyResult: "نسخ السر المستعاد (الاستعادة فقط)",
    shortcutShowHelp: "عرض الاختصارات",
    shortcutShowKeytips: "إظهار تلميحات الاختصارات",
  },
} as const;

type StringsShape = (typeof STRINGS)["en"];

export type Strings = {
  [K in keyof StringsShape]: string;
};
