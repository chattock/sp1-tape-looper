/* Selected MP4 AAC presentation length, using exact stream time-base arithmetic.
 * Decoder tail padding may be clipped; unrelated formats are unchanged. */
self.aacPresentation473 = function (stream) {
  if (stream.codec_name !== 'aac' || stream.codec_tag_string !== 'mp4a') return null;
  const ticks = stream.duration_ts;
  if ((typeof ticks === 'number' && !Number.isSafeInteger(ticks)) || !/^[1-9][0-9]{0,15}$/.test(String(ticks))) throw Error('AAC presentation duration is unavailable; export a bounded WAV');
  const base = /^([1-9][0-9]{0,9})\/([1-9][0-9]{0,9})$/.exec(String(stream.time_base));
  const rate = Number(stream.sample_rate);
  if (!base || !Number.isSafeInteger(rate) || rate < 1 || rate > 768000) throw Error('AAC presentation time base is unsupported; export a WAV');
  const denominator = BigInt(base[2]);
  const frames = (BigInt(ticks) * BigInt(base[1]) * 48000n + denominator / 2n) / denominator;
  if (frames < 1n || frames > 23045120n) throw Error('AAC presentation exceeds the preparation limit');
  return { frames: Number(frames), maxPaddingFrames: Math.ceil(4096 * 48000 / rate) + 2 };
};
self.clipAACPresentation473 = function (raw, channels, stream) {
  const presentation = self.aacPresentation473(stream);
  if (!presentation) return { raw, presentation: null };
  const actual = raw.length / (channels * 4), extra = actual - presentation.frames;
  if (!Number.isInteger(actual) || extra < 0 || extra > presentation.maxPaddingFrames) throw Error('Decoded AAC does not match its presentation length; export a WAV');
  return {raw: raw.subarray(0, presentation.frames * channels * 4), presentation: {frames: presentation.frames, discardedPaddingFrames: extra}};
};
