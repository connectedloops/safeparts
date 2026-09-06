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
    errorInvalidShare: "Check that each Recovery share is complete and uses the selected encoding. If it came from a newer Safeparts version, use a compatible version to recover it.",
    errorDuplicateShare: "Each Recovery share must be different. Replace repeated shares with other shares from the same set.",
    errorInconsistentShares: "These Recovery shares do not form one consistent set. Use shares from the same Split operation without changing their contents.",
    errorPassphraseRequired: "Enter the passphrase used to protect these Recovery shares. Safeparts cannot reset it.",
    errorDecryptionFailed: "The passphrase may be wrong or the encrypted data may have been changed. Check the passphrase and use intact Recovery shares from the same set.",
    errorUnsupportedParameters: "These Recovery shares use unsupported packet or encryption parameters. Use a compatible Safeparts version; do not edit the shares or their parameters.",
    errorUnsupportedEncoding: "Select the encoding used for these Recovery shares and try again.",
    errorRecoveryFailed: "Recovery could not finish. Check your Recovery shares and try again. If it still fails, reload Safeparts and re-enter the shares from your saved copies.",
    errorRecoveredSecretNotText:
      "This recovered Secret is not valid UTF-8 text. Use the CLI or TUI with file output to recover the exact bytes.",
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
    errorInvalidShare: "تحقق من اكتمال كل حصة استرداد ومن مطابقتها للصيغة المختارة. إذا أُنشئت بإصدار أحدث من Safeparts، فاستخدم إصدارًا متوافقًا لاستعادتها.",
    errorDuplicateShare: "يجب أن تكون كل حصة استرداد مختلفة. استبدل الحصص المكررة بحصص أخرى من المجموعة نفسها.",
    errorInconsistentShares: "حصص الاسترداد هذه لا تنتمي إلى مجموعة متسقة واحدة. استخدم حصصًا من عملية التقسيم نفسها دون تغيير محتواها.",
    errorPassphraseRequired: "أدخل عبارة المرور المستخدمة لحماية حصص الاسترداد هذه. لا يستطيع Safeparts إعادة تعيينها.",
    errorDecryptionFailed: "قد تكون عبارة المرور خاطئة أو ربما تغيرت البيانات المشفرة. تحقق من عبارة المرور واستخدم حصص استرداد سليمة من المجموعة نفسها.",
    errorUnsupportedParameters: "تستخدم حصص الاسترداد هذه معاملات غير مدعومة للحزمة أو التشفير. استخدم إصدارًا متوافقًا من Safeparts ولا تعدّل الحصص أو معاملاتها.",
    errorUnsupportedEncoding: "اختر الصيغة المستخدمة لحصص الاسترداد هذه ثم حاول مرة أخرى.",
    errorRecoveryFailed: "تعذر إكمال الاستعادة. تحقق من حصص الاسترداد وحاول مرة أخرى. إذا استمر الخطأ، فأعد تحميل Safeparts وأدخل الحصص من نسخك المحفوظة.",
    errorRecoveredSecretNotText:
      "السر المستعاد ليس نصًا صالحًا بترميز UTF-8. استخدم CLI أو TUI مع ملف إخراج لاستعادة البايتات بدقة.",
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
