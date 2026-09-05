import type { Strings } from '../i18n'

// Only documented numeric fields cross into presentation. Legacy string errors,
// unknown codes and unexpected exceptions deliberately share the safe fallback.
export function recoveryFeedback(error: unknown, strings: Strings): { message: string; missing?: number } {
  const fallback = { message: strings.errorRecoveryFailed }
  if (typeof error !== 'object' || error === null || !('code' in error)) return fallback
  switch (error.code) {
    case 'invalid_share': return { message: strings.errorInvalidShare }
    case 'insufficient_shares': {
      if (!('required' in error) || !('provided' in error)) return fallback
      const { required, provided } = error
      if (typeof required !== 'number' || typeof provided !== 'number' ||
          !Number.isInteger(required) || !Number.isInteger(provided) ||
          required < 1 || required > 255 || provided < 0 || provided >= required) return fallback
      const missing = required - provided
      return {
        message: missing === 1 ? strings.errorNotEnoughSharesOne : strings.errorNotEnoughSharesMany.replace('{missing}', String(missing)),
        missing,
      }
    }
    case 'duplicate_share': return { message: strings.errorDuplicateShare }
    case 'inconsistent_shares': return { message: strings.errorInconsistentShares }
    case 'passphrase_required': return { message: strings.errorPassphraseRequired }
    case 'decryption_failed': return { message: strings.errorDecryptionFailed }
    case 'unsupported_parameters': return { message: strings.errorUnsupportedParameters }
    case 'unsupported_encoding': return { message: strings.errorUnsupportedEncoding }
    default: return fallback
  }
}
