export function cursorTo(stream,column,row) {
  stream.puts(`\x1b[${row};${column}H`);
  stream.flush();
}

export function clearLine(stream, dir) {
  stream.puts(`\x1b[${[1, 2, 0][dir + 1]}K`);
  stream.flush();
}
