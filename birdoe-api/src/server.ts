import express from "express";
import { mkdir, writeFile } from "node:fs/promises";
import path from "node:path";

const app = express();
const PORT = 3000;

app.use(
  "/capture",
  express.raw({
    type: ["image/jpeg", "application/octet-stream"],
    limit: "5mb",
  })
);

app.post("/capture", async (req, res) => {
  const device = String(req.header("x-device") ?? "unknown");
  const rssi = req.header("x-rssi");
  const battery = req.header("x-battery-mv");

  if (!Buffer.isBuffer(req.body) || req.body.length === 0) {
    return res.status(400).json({ error: "Empty body" });
  }

  const now = new Date();
  const dir = path.join(
    "captures",
    now.toISOString().slice(0, 10),
    device.replace(/[^a-zA-Z0-9_-]/g, "_")
  );

  await mkdir(dir, { recursive: true });

  const filename = `${now.toISOString().replace(/[:.]/g, "-")}.jpg`;
  const filepath = path.join(dir, filename);

  await writeFile(filepath, req.body);

  console.log({
    device,
    bytes: req.body.length,
    rssi,
    battery,
    filepath,
  });

    res.status(201).type("text/plain").send("OK");
});

app.get("/health", (_req, res) => {
  res.json({ ok: true });
});

app.listen(PORT, "0.0.0.0", () => {
  console.log(`Birdbox API listening on http://0.0.0.0:${PORT}`);
});

app.use((err: unknown, _req: express.Request, res: express.Response, _next: express.NextFunction) => {
  console.error("Unhandled request error:", err);
  if (!res.headersSent) {
    res.status(500).type("text/plain").send("ERROR");
  }
});