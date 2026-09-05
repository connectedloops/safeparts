import { useEffect, useState } from 'react'
import { useAnnouncement } from '../context/LiveRegionContext'

type CopyButtonProps = {
  value: string
  copyLabel: string
  copiedLabel: string
  copyFailedLabel: string
  accessibleLabel: string
  className?: string
  announceCopied?: string
  keytip?: string
  shortcut?: 'copy-result'
}

function CopyIcon() {
  return (
    <svg viewBox="0 0 24 24" fill="none" className="h-4 w-4" aria-hidden="true">
      <path
        d="M9 9h10v10H9V9Z"
        stroke="currentColor"
        strokeWidth="2"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <path
        d="M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1"
        stroke="currentColor"
        strokeWidth="2"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  )
}

function CheckIcon() {
  return (
    <svg viewBox="0 0 24 24" fill="none" className="h-4 w-4" aria-hidden="true">
      <path
        d="m20 6-11 11-5-5"
        stroke="currentColor"
        strokeWidth="2"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  )
}

async function copyToClipboard(text: string) {
  if (typeof navigator !== 'undefined' && navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
    await navigator.clipboard.writeText(text)
    return
  }

  const textarea = document.createElement('textarea')
  textarea.value = text
  textarea.setAttribute('readonly', 'true')
  textarea.style.position = 'fixed'
  textarea.style.top = '0'
  textarea.style.left = '0'
  textarea.style.opacity = '0'

  document.body.appendChild(textarea)
  textarea.select()
  try {
    if (!document.execCommand('copy')) {
      throw new Error('clipboard write failed')
    }
  } finally {
    document.body.removeChild(textarea)
  }
}

export function CopyButton({
  value,
  copyLabel,
  copiedLabel,
  copyFailedLabel,
  accessibleLabel,
  className,
  announceCopied,
  keytip,
  shortcut,
}: CopyButtonProps) {
  const [copied, setCopied] = useState(false)
  const { announce } = useAnnouncement()

  useEffect(() => {
    if (!copied) return
    const handle = window.setTimeout(() => setCopied(false), 1200)
    return () => window.clearTimeout(handle)
  }, [copied])

  async function onCopy() {
    try {
      await copyToClipboard(value)
      setCopied(true)
      if (announceCopied) {
        announce(announceCopied, 'polite')
      }
    } catch {
      announce(copyFailedLabel, 'assertive')
    }
  }

  return (
    <button
      type="button"
      data-keytip={keytip}
      data-shortcut={shortcut}
      aria-label={accessibleLabel}
      className={`btn-secondary px-3 py-2.5 text-xs min-h-[44px] min-w-[44px] ${className ? className : ''}`}
      onClick={onCopy}
    >
      {copied ? <CheckIcon /> : <CopyIcon />}
      <span>{copied ? copiedLabel : copyLabel}</span>
    </button>
  )
}
