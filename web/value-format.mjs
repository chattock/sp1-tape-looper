// Prefer an exact whole-percent label when it encodes back to this native value.
// Never turn a below-maximum value into100. Raw telemetry and DSP are unchanged.
export function formatValue(value,max=255){
 const percent=value/max*100,whole=Math.round(percent);
 return (Math.round(whole*max/100)===value?whole:percent).toFixed(1);
}
