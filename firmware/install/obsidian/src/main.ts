import {
  App,
  Notice,
  Plugin,
  PluginSettingTab,
  Setting,
  TFile,
  TFolder,
  normalizePath,
  requestUrl,
} from "obsidian";

type Provider = "drive" | "github";

interface MJSettings {
  provider: Provider;
  // Drive (Apps Script)
  url: string;
  token: string;
  // GitHub
  ghOwner: string;
  ghRepo: string;
  ghBranch: string;
  ghPath: string; // sub-folder in the repo ("" = root)
  ghToken: string;
  // shared
  folder: string;
}

const DEFAULTS: MJSettings = {
  provider: "drive",
  url: "",
  token: "",
  ghOwner: "",
  ghRepo: "",
  ghBranch: "main",
  ghPath: "",
  ghToken: "",
  folder: "MicroJournal",
};

// Per-note record of the last successful sync, KEYED BY REMOTE ID so a rename on
// either side is recognized as the same file instead of a duplicate. For Drive
// the id is the stable Drive file id; for GitHub it's the repo path (which moves
// on rename — handled by re-keying the state in runSync).
interface SyncRecord {
  driveName: string; // last-synced remote filename (e.g. "Title-0.txt")
  localPath: string; // current vault path (kept fresh by the rename listener)
  remoteModified: number;
  remoteSize: number; // UTF-8 byte length of the content we last reconciled
  localMtime: number;
  hash: string; // content hash at last sync
}

interface RemoteFile {
  id: string;
  name: string;
  modified: number;
  size: number;
}

// The seam the reconcile engine talks to. Drive and GitHub each implement it.
interface RemoteProvider {
  list(): Promise<RemoteFile[]>;
  get(id: string): Promise<{ content: string; modified: number; name: string }>; // content = base64
  // create OR update a file by name/path; returns the resulting id
  create(name: string, content: string): Promise<{ id: string; name: string; modified: number }>;
  // rename; returns the (possibly NEW) id so the engine can re-key its state
  rename(id: string, newName: string): Promise<{ id: string; name: string; modified: number }>;
  trash(id: string): Promise<void>;
}

interface PersistedData {
  settings: MJSettings;
  state: Record<string, SyncRecord>; // keyed by remote id
}

// Remote holds .txt (what the device writes); Obsidian shows .md.
const stripTxt = (name: string) => name.replace(/\.txt$/i, "");
const baseToTxt = (base: string) => base + ".txt";

export default class MicroJournalSync extends Plugin {
  settings: MJSettings = { ...DEFAULTS };
  state: Record<string, SyncRecord> = {};
  syncing = false;
  prov!: RemoteProvider;

  async onload() {
    await this.loadAll();

    this.addRibbonIcon("refresh-cw", "Sync MicroJournal", () => this.syncNow());
    this.addCommand({
      id: "sync-now",
      name: "Sync MicroJournal now",
      callback: () => this.syncNow(),
    });
    this.addSettingTab(new MJSettingTab(this.app, this));

    // Keep each record's localPath current so a vault rename is detected as a
    // rename (and pushed to the remote) rather than delete-old + create-new.
    this.registerEvent(
      this.app.vault.on("rename", async (file, oldPath) => {
        if (!(file instanceof TFile)) return;
        let changed = false;
        for (const id of Object.keys(this.state)) {
          if (this.state[id].localPath === oldPath) {
            this.state[id].localPath = file.path;
            changed = true;
          }
        }
        if (changed) await this.saveAll();
      })
    );
  }

  async loadAll() {
    const data = (await this.loadData()) as PersistedData | null;
    this.settings = Object.assign({}, DEFAULTS, data?.settings);
    this.state = data?.state ?? {};
  }

  async saveAll() {
    const data: PersistedData = { settings: this.settings, state: this.state };
    await this.saveData(data);
  }

  // Build the active backend from settings.
  makeProvider(): RemoteProvider {
    if (this.settings.provider === "github") {
      return new GitHubProvider(
        this.settings.ghOwner,
        this.settings.ghRepo,
        this.settings.ghBranch || "main",
        this.settings.ghPath,
        this.settings.ghToken
      );
    }
    return new DriveProvider(this.settings.url, this.settings.token);
  }

  // Provider-specific "is it configured?" check for the sync/test guards.
  private configError(): string | null {
    if (this.settings.provider === "github") {
      if (!this.settings.ghOwner || !this.settings.ghRepo) return "set GitHub owner/repo in settings first";
      if (!this.settings.ghToken) return "set the GitHub token in settings first";
      return null;
    }
    if (!this.settings.url) return "set the Web App URL in settings first";
    return null;
  }

  async syncNow() {
    if (this.syncing) {
      new Notice("MicroJournal: sync already running");
      return;
    }
    const cfgErr = this.configError();
    if (cfgErr) {
      new Notice("MicroJournal: " + cfgErr);
      return;
    }
    this.prov = this.makeProvider();
    this.syncing = true;
    const notice = new Notice("MicroJournal: syncing…", 0);
    try {
      const r = await this.runSync();
      notice.setMessage(
        `MicroJournal: ↓${r.pulled} ↑${r.pushed}` +
          (r.renamed ? ` ⇄${r.renamed}` : "") +
          (r.deleted ? ` ⌫${r.deleted}` : "") +
          (r.conflicts ? ` ⚠${r.conflicts}` : "") +
          (r.errors ? ` ✗${r.errors}` : "")
      );
    } catch (e) {
      console.error("MicroJournal sync failed", e);
      notice.setMessage("MicroJournal: sync failed — see console");
    } finally {
      window.setTimeout(() => notice.hide(), 6000);
      this.syncing = false;
      await this.saveAll();
    }
  }

  private vaultPathFor(folder: string, base: string) {
    return normalizePath(folder + "/" + base + ".md");
  }

  // First free "<base> (conflict).md" / "<base> (conflict N).md" in the folder.
  private uniqueConflictPath(folder: string, base: string): string {
    for (let n = 0; ; n++) {
      const suffix = n === 0 ? " (conflict)" : ` (conflict ${n + 1})`;
      const p = normalizePath(folder + "/" + base + suffix + ".md");
      if (!this.app.vault.getAbstractFileByPath(p)) return p;
    }
  }

  // Move a state record to a new id (GitHub renames change the path-id; Drive
  // ids never move so this is a no-op there).
  private rekey(oldId: string, newId: string) {
    if (newId === oldId) return;
    this.state[newId] = this.state[oldId];
    delete this.state[oldId];
  }

  private async runSync() {
    const folderPath = normalizePath(this.settings.folder);
    await this.ensureFolder(folderPath);

    const remoteList = await this.prov.list();
    const remoteById = new Map<string, RemoteFile>();
    for (const f of remoteList) remoteById.set(f.id, f);

    const localFiles: TFile[] = [];
    for (const file of this.app.vault.getFiles()) {
      if (file.parent?.path !== folderPath) continue;
      if (file.extension.toLowerCase() !== "md") continue;
      localFiles.push(file);
    }
    const localByPath = new Map<string, TFile>();
    for (const f of localFiles) localByPath.set(f.path, f);

    let pulled = 0,
      pushed = 0,
      renamed = 0,
      deleted = 0,
      conflicts = 0,
      errors = 0;
    const goneIds = new Set<string>(); // remote ids we trashed this run

    // ---- 1) reconcile notes we already know (state keyed by remote id) ------
    for (const id of Object.keys(this.state)) {
      const rec = this.state[id];
      const r = remoteById.get(id);
      const l = localByPath.get(rec.localPath);
      try {
        if (!r && !l) {
          delete this.state[id];
          continue;
        }

        // local file gone, remote file still there
        if (r && !l) {
          if (r.modified === rec.remoteModified) {
            await this.prov.trash(id); // local delete → propagate to remote
            goneIds.add(id);
            delete this.state[id];
            deleted++;
          } else {
            // edit on remote beats the local delete → recreate locally
            const f = await this.pullToPath(id, this.vaultPathFor(folderPath, stripTxt(r.name)));
            const c = await this.app.vault.read(f);
            this.state[id] = {
              driveName: r.name,
              localPath: f.path,
              remoteModified: r.modified,
              remoteSize: utf8len(c),
              localMtime: f.stat.mtime,
              hash: hash(c),
            };
            pulled++;
          }
          continue;
        }

        // remote file gone (id no longer exists), local still there
        if (!r && l) {
          const content = await this.app.vault.read(l);
          if (hash(content) === rec.hash) {
            await this.app.vault.trash(l, true); // remote delete → trash locally
            delete this.state[id];
            deleted++;
          } else {
            // edit locally beats the remote delete → recreate on the remote
            const res = await this.prov.create(baseToTxt(l.basename), content);
            delete this.state[id];
            this.state[res.id] = {
              driveName: res.name,
              localPath: l.path,
              remoteModified: res.modified,
              remoteSize: utf8len(content),
              localMtime: l.stat.mtime,
              hash: hash(content),
            };
            pushed++;
          }
          continue;
        }

        // both exist — reconcile NAME first, then content
        const remoteBase = stripTxt(r!.name);
        const recBase = stripTxt(rec.driveName);
        const localBase = l!.basename;
        const remoteRenamed = r!.name !== rec.driveName;
        const localRenamed = localBase !== recBase;

        let curId = id; // tracks the record's id across a remote-side rename
        if (remoteBase !== localBase) {
          if (remoteRenamed && !localRenamed) {
            // remote rename → rename the vault note to match
            const newPath = this.vaultPathFor(folderPath, remoteBase);
            await this.app.fileManager.renameFile(l!, newPath);
            rec.driveName = r!.name;
            rec.localPath = newPath;
            rec.remoteModified = r!.modified;
            renamed++;
          } else if (localRenamed && !remoteRenamed) {
            // vault rename → rename the remote file to match
            const res = await this.prov.rename(id, baseToTxt(localBase));
            this.rekey(id, res.id);
            if (res.id !== id) goneIds.add(id); // old id (e.g. git path) is retired
            curId = res.id;
            rec.driveName = res.name;
            rec.remoteModified = res.modified;
            renamed++;
          } else if (localRenamed && remoteRenamed) {
            // both renamed differently → remote wins
            const newPath = this.vaultPathFor(folderPath, remoteBase);
            await this.app.fileManager.renameFile(l!, newPath);
            rec.driveName = r!.name;
            rec.localPath = newPath;
            rec.remoteModified = r!.modified;
            renamed++;
            conflicts++;
          }
        }

        // content sync — re-resolve the local handle (path may have changed)
        const lf = this.app.vault.getAbstractFileByPath(rec.localPath);
        const localFile = lf instanceof TFile ? lf : l!;
        const content = await this.app.vault.read(localFile);
        const localHash = hash(content);
        const localChanged = localHash !== rec.hash;
        // Detect remote edits by timestamp OR content byte-size. Size catches the
        // case where the listed modified-time advances but a content fetch briefly
        // lagged, which would otherwise wedge the record permanently.
        const remoteChanged =
          r!.modified !== rec.remoteModified ||
          (rec.remoteSize !== undefined && rec.remoteSize !== r!.size);

        if (localChanged && remoteChanged) {
          // Both sides edited since the last sync — never silently overwrite.
          const remoteContent = b64ToUtf8((await this.prov.get(curId)).content);
          if (hash(remoteContent) === localHash) {
            // they happen to be identical → not a real conflict
            rec.remoteModified = r!.modified;
            rec.remoteSize = utf8len(content);
            rec.localMtime = localFile.stat.mtime;
            rec.hash = localHash;
          } else {
            // keep BOTH: save the remote version as a conflict copy, keep the
            // local note as canonical, and push local so the tracked pair
            // converges. The conflict copy syncs up as its own file next run.
            const conflictPath = this.uniqueConflictPath(folderPath, localFile.basename);
            await this.app.vault.create(conflictPath, remoteContent);
            const res = await this.prov.create(rec.driveName, content);
            this.rekey(curId, res.id);
            rec.remoteModified = res.modified;
            rec.remoteSize = utf8len(content);
            rec.localMtime = localFile.stat.mtime;
            rec.hash = localHash;
            conflicts++;
          }
        } else if (remoteChanged) {
          await this.pullInto(curId, localFile);
          const c = await this.app.vault.read(localFile);
          rec.remoteModified = r!.modified;
          rec.remoteSize = utf8len(c);
          rec.localMtime = localFile.stat.mtime;
          rec.hash = hash(c);
          pulled++;
        } else if (localChanged) {
          const res = await this.prov.create(rec.driveName, content);
          this.rekey(curId, res.id);
          rec.remoteModified = res.modified;
          rec.remoteSize = utf8len(content);
          rec.localMtime = localFile.stat.mtime;
          rec.hash = localHash;
          pushed++;
        } else if (rec.localMtime !== localFile.stat.mtime) {
          rec.localMtime = localFile.stat.mtime; // unchanged, just refresh mtime
        }
      } catch (e) {
        console.error(`MicroJournal: id ${id} failed`, e);
        errors++;
      }
    }

    // ---- 2) new remote files (id not tracked) → pull as new -----------------
    for (const [id, r] of remoteById) {
      if (this.state[id] || goneIds.has(id)) continue;
      try {
        const f = await this.pullToPath(id, this.vaultPathFor(folderPath, stripTxt(r.name)));
        const c = await this.app.vault.read(f);
        this.state[id] = {
          driveName: r.name,
          localPath: f.path,
          remoteModified: r.modified,
          remoteSize: utf8len(c),
          localMtime: f.stat.mtime,
          hash: hash(c),
        };
        pulled++;
      } catch (e) {
        console.error(`MicroJournal: pull new ${id} failed`, e);
        errors++;
      }
    }

    // ---- 3) new vault notes (not tied to any record) → push as new ----------
    const recordedPaths = new Set(Object.values(this.state).map((r) => r.localPath));
    for (const l of localFiles) {
      if (recordedPaths.has(l.path)) continue;
      if (!this.app.vault.getAbstractFileByPath(l.path)) continue; // trashed earlier this run
      try {
        const content = await this.app.vault.read(l);
        const res = await this.prov.create(baseToTxt(l.basename), content);
        this.state[res.id] = {
          driveName: res.name,
          localPath: l.path,
          remoteModified: res.modified,
          remoteSize: utf8len(content),
          localMtime: l.stat.mtime,
          hash: hash(content),
        };
        pushed++;
      } catch (e) {
        console.error(`MicroJournal: push new ${l.path} failed`, e);
        errors++;
      }
    }

    return { pulled, pushed, renamed, deleted, conflicts, errors };
  }

  // Fetch a remote file by id and write it to `path` (create or overwrite).
  private async pullToPath(id: string, path: string): Promise<TFile> {
    const content = b64ToUtf8((await this.prov.get(id)).content);
    const existing = this.app.vault.getAbstractFileByPath(path);
    if (existing instanceof TFile) {
      await this.app.vault.modify(existing, content);
      return existing;
    }
    return await this.app.vault.create(path, content);
  }

  // Fetch a remote file by id and overwrite an existing vault file.
  private async pullInto(id: string, file: TFile): Promise<void> {
    const content = b64ToUtf8((await this.prov.get(id)).content);
    await this.app.vault.modify(file, content);
  }

  async testConnection(): Promise<{ ok: boolean; message: string }> {
    const cfgErr = this.configError();
    if (cfgErr) return { ok: false, message: cfgErr };
    try {
      const files = await this.makeProvider().list();
      const where = this.settings.provider === "github" ? "GitHub" : "Drive";
      return { ok: true, message: `connected — ${files.length} file(s) in ${where}` };
    } catch (e: any) {
      const msg = (e && e.message) || String(e);
      return {
        ok: false,
        message: /unauthorized|401|403/i.test(msg) ? "unauthorized — check the token" : msg,
      };
    }
  }

  private async ensureFolder(path: string) {
    const existing = this.app.vault.getAbstractFileByPath(path);
    if (existing instanceof TFolder) return;
    if (!existing) await this.app.vault.createFolder(path);
  }
}

// ---- Drive provider (Apps Script Web App) ----------------------------------
class DriveProvider implements RemoteProvider {
  constructor(private url: string, private token: string) {}

  private withParams(params: Record<string, string>) {
    const url = new URL(this.url);
    if (this.token) url.searchParams.set("token", this.token);
    for (const [k, v] of Object.entries(params)) url.searchParams.set(k, v);
    return url.toString();
  }

  private async getJson(params: Record<string, string>, method: "GET" | "POST", body?: string) {
    const r = await requestUrl({
      url: this.withParams(params),
      method,
      ...(body != null ? { contentType: "text/plain", body } : {}),
    });
    const j = r.json;
    if (j.status !== "OK") throw new Error(j.message || "request failed");
    return j;
  }

  async list(): Promise<RemoteFile[]> {
    return (await this.getJson({ action: "list" }, "GET")).files as RemoteFile[];
  }

  async get(id: string): Promise<{ content: string; modified: number; name: string }> {
    const j = await this.getJson({ action: "get", id }, "GET");
    return { content: j.content, modified: j.modified, name: j.name };
  }

  async create(name: string, content: string): Promise<{ id: string; name: string; modified: number }> {
    const j = await this.getJson({ name }, "POST", utf8ToB64(content));
    return { id: j.id, name: j.name, modified: j.modified };
  }

  async rename(id: string, name: string): Promise<{ id: string; name: string; modified: number }> {
    const j = await this.getJson({ action: "rename", id, name }, "GET");
    return { id, name: j.name, modified: j.modified }; // Drive id is stable
  }

  async trash(id: string): Promise<void> {
    await this.getJson({ action: "trash", id }, "GET");
  }
}

// ---- GitHub provider (Contents API + Git Data single-commit rename) --------
class GitHubProvider implements RemoteProvider {
  private dir: string;
  constructor(
    private owner: string,
    private repo: string,
    private branch: string,
    dir: string,
    private token: string
  ) {
    this.dir = (dir || "").replace(/^\/+|\/+$/g, "");
  }

  private headers() {
    return {
      Authorization: "Bearer " + this.token,
      Accept: "application/vnd.github+json",
      "User-Agent": "microjournal-sync",
      "X-GitHub-Api-Version": "2022-11-28",
    };
  }

  private api(rest: string) {
    return `https://api.github.com/repos/${this.owner}/${this.repo}/${rest}`;
  }

  // encode each path segment but keep the "/" separators
  private enc(path: string) {
    return path.split("/").map(encodeURIComponent).join("/");
  }

  private contentsUrl(path: string) {
    return this.api("contents/" + this.enc(path));
  }

  private fullPath(name: string) {
    return this.dir ? this.dir + "/" + name : name;
  }

  async list(): Promise<RemoteFile[]> {
    const r = await requestUrl({
      url: this.contentsUrl(this.dir) + `?ref=${encodeURIComponent(this.branch)}`,
      headers: this.headers(),
      throw: false,
    });
    if (r.status === 404) return []; // empty repo / dir not created yet
    if (r.status >= 400) throw new Error(`GitHub list ${r.status}`);
    const arr = r.json as any[];
    if (!Array.isArray(arr)) return [];
    return arr
      .filter((x) => x.type === "file" && /\.txt$/i.test(x.name))
      .map((x) => ({ id: x.path, name: x.name, modified: numFromSha(x.sha), size: x.size }));
  }

  async get(id: string): Promise<{ content: string; modified: number; name: string }> {
    const r = await requestUrl({
      url: this.contentsUrl(id) + `?ref=${encodeURIComponent(this.branch)}`,
      headers: this.headers(),
      throw: false,
    });
    if (r.status >= 400) throw new Error(`GitHub get ${r.status}`);
    const j = r.json;
    return { content: (j.content || "").replace(/\n/g, ""), modified: numFromSha(j.sha), name: baseName(id) };
  }

  // current blob sha of a path, or null if it doesn't exist
  private async shaOf(path: string): Promise<string | null> {
    const r = await requestUrl({
      url: this.contentsUrl(path) + `?ref=${encodeURIComponent(this.branch)}`,
      headers: this.headers(),
      throw: false,
    });
    return r.status < 300 ? (r.json.sha as string) : null;
  }

  async create(name: string, content: string): Promise<{ id: string; name: string; modified: number }> {
    const path = this.fullPath(name);
    const sha = await this.shaOf(path);
    const body: any = { message: `obsidian: ${name}`, content: utf8ToB64(content), branch: this.branch };
    if (sha) body.sha = sha;
    const r = await requestUrl({
      url: this.contentsUrl(path),
      method: "PUT",
      headers: { ...this.headers(), "Content-Type": "application/json" },
      body: JSON.stringify(body),
      throw: false,
    });
    if (r.status >= 400) throw new Error(`GitHub create ${r.status}: ${r.text}`);
    return { id: path, name, modified: numFromSha(r.json.content.sha) };
  }

  // Pure rename via a single Git Data commit (reuse the old blob → git follows
  // the rename and history stays continuous). Content edits, if any, are pushed
  // separately by the engine's content-sync step right after.
  async rename(id: string, newName: string): Promise<{ id: string; name: string; modified: number }> {
    const newPath = this.fullPath(newName);
    const oldSha = await this.shaOf(id);
    if (!oldSha) throw new Error("GitHub rename: source missing");
    await this.gitMove(id, newPath, oldSha, `obsidian: rename -> ${newName}`);
    return { id: newPath, name: newName, modified: numFromSha(oldSha) };
  }

  async trash(id: string): Promise<void> {
    const sha = await this.shaOf(id);
    if (!sha) return; // already gone
    await requestUrl({
      url: this.contentsUrl(id),
      method: "DELETE",
      headers: { ...this.headers(), "Content-Type": "application/json" },
      body: JSON.stringify({ message: `obsidian: delete ${baseName(id)}`, sha, branch: this.branch }),
      throw: false,
    });
  }

  // ref → base tree → tree(add newPath=blob, remove oldPath) → commit → ref
  private async gitMove(oldPath: string, newPath: string, blobSha: string, msg: string) {
    const h = this.headers();
    const jh = { ...h, "Content-Type": "application/json" };
    const get = async (rest: string) => {
      const r = await requestUrl({ url: this.api(rest), headers: h, throw: false });
      if (r.status >= 400) throw new Error(`GitHub ${rest} ${r.status}`);
      return r.json;
    };
    const post = async (rest: string, body: any, method = "POST") => {
      const r = await requestUrl({ url: this.api(rest), method, headers: jh, body: JSON.stringify(body), throw: false });
      if (r.status >= 400) throw new Error(`GitHub ${rest} ${r.status}: ${r.text}`);
      return r.json;
    };
    const ref = await get(`git/ref/heads/${this.branch}`);
    const headSha = ref.object.sha;
    const commit = await get(`git/commits/${headSha}`);
    const baseTree = commit.tree.sha;
    const tree = await post(`git/trees`, {
      base_tree: baseTree,
      tree: [
        { path: newPath, mode: "100644", type: "blob", sha: blobSha },
        { path: oldPath, mode: "100644", type: "blob", sha: null }, // delete old
      ],
    });
    const newCommit = await post(`git/commits`, { message: msg, tree: tree.sha, parents: [headSha] });
    await post(`git/refs/heads/${this.branch}`, { sha: newCommit.sha, force: false }, "PATCH");
  }
}

// ---- helpers ---------------------------------------------------------------
// djb2 over the UTF-8 code units; enough to detect content edits.
function hash(s: string): string {
  let h = 5381;
  for (let i = 0; i < s.length; i++) h = ((h << 5) + h + s.charCodeAt(i)) | 0;
  return (h >>> 0).toString(16) + ":" + s.length;
}

function utf8len(s: string): number {
  return new TextEncoder().encode(s).length;
}

function utf8ToB64(s: string): string {
  const bytes = new TextEncoder().encode(s);
  let bin = "";
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return btoa(bin);
}

function b64ToUtf8(b64: string): string {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return new TextDecoder("utf-8").decode(bytes);
}

const baseName = (path: string) => path.slice(path.lastIndexOf("/") + 1);

// A numeric change-token derived from a git blob sha (GitHub has no mtime). The
// first 13 hex digits fit in a JS safe integer and change whenever content does.
function numFromSha(sha: string): number {
  return sha ? parseInt(sha.slice(0, 13), 16) : 0;
}

class MJSettingTab extends PluginSettingTab {
  constructor(app: App, private plugin: MicroJournalSync) {
    super(app, plugin);
  }

  display(): void {
    const { containerEl } = this;
    containerEl.empty();

    new Setting(containerEl)
      .setName("Sync provider")
      .setDesc("Which backend to mirror with. Switching providers resets the sync state.")
      .addDropdown((d) =>
        d
          .addOption("drive", "Google Drive")
          .addOption("github", "GitHub")
          .setValue(this.plugin.settings.provider)
          .onChange(async (v) => {
            if (v !== this.plugin.settings.provider) {
              this.plugin.settings.provider = v as Provider;
              // Drive ids and git paths are different id namespaces — a stale
              // state map would mass-duplicate. Start fresh on every switch.
              this.plugin.state = {};
              await this.plugin.saveAll();
              new Notice("MicroJournal: provider switched — sync state reset");
              this.display();
            }
          })
      );

    if (this.plugin.settings.provider === "drive") {
      new Setting(containerEl)
        .setName("Web App URL")
        .setDesc("The Apps Script /exec URL (without ?token).")
        .addText((t) =>
          t
            .setPlaceholder("https://script.google.com/macros/s/…/exec")
            .setValue(this.plugin.settings.url)
            .onChange(async (v) => {
              this.plugin.settings.url = v.trim();
              await this.plugin.saveAll();
            })
        );

      new Setting(containerEl)
        .setName("Token")
        .setDesc("Shared secret set in the Apps Script (_TOKEN). Leave empty if the endpoint is open.")
        .addText((t) => {
          t.setValue(this.plugin.settings.token).onChange(async (v) => {
            this.plugin.settings.token = v.trim();
            await this.plugin.saveAll();
          });
          t.inputEl.type = "password";
        });
    } else {
      new Setting(containerEl)
        .setName("Owner")
        .setDesc("GitHub username or org that owns the repo.")
        .addText((t) =>
          t.setPlaceholder("alfarhan").setValue(this.plugin.settings.ghOwner).onChange(async (v) => {
            this.plugin.settings.ghOwner = v.trim();
            await this.plugin.saveAll();
          })
        );

      new Setting(containerEl)
        .setName("Repository")
        .addText((t) =>
          t.setPlaceholder("mj").setValue(this.plugin.settings.ghRepo).onChange(async (v) => {
            this.plugin.settings.ghRepo = v.trim();
            await this.plugin.saveAll();
          })
        );

      new Setting(containerEl)
        .setName("Branch")
        .addText((t) =>
          t.setPlaceholder("main").setValue(this.plugin.settings.ghBranch).onChange(async (v) => {
            this.plugin.settings.ghBranch = v.trim() || "main";
            await this.plugin.saveAll();
          })
        );

      new Setting(containerEl)
        .setName("Path")
        .setDesc("Sub-folder in the repo to sync. Leave empty for the repo root.")
        .addText((t) =>
          t.setPlaceholder("(root)").setValue(this.plugin.settings.ghPath).onChange(async (v) => {
            this.plugin.settings.ghPath = v.trim();
            await this.plugin.saveAll();
          })
        );

      new Setting(containerEl)
        .setName("Token")
        .setDesc("Fine-grained PAT with Contents: Read and write on this repo.")
        .addText((t) => {
          t.setPlaceholder("github_pat_…").setValue(this.plugin.settings.ghToken).onChange(async (v) => {
            this.plugin.settings.ghToken = v.trim();
            await this.plugin.saveAll();
          });
          t.inputEl.type = "password";
        });
    }

    new Setting(containerEl)
      .setName("Vault folder")
      .setDesc("Folder that mirrors the remote MicroJournal files.")
      .addText((t) =>
        t.setValue(this.plugin.settings.folder).onChange(async (v) => {
          this.plugin.settings.folder = v.trim() || DEFAULTS.folder;
          await this.plugin.saveAll();
        })
      );

    new Setting(containerEl)
      .setName("Test connection")
      .setDesc("Check the settings by listing the remote folder.")
      .addButton((b) =>
        b.setButtonText("Test").onClick(async () => {
          b.setButtonText("Testing…").setDisabled(true);
          const r = await this.plugin.testConnection();
          b.setButtonText("Test").setDisabled(false);
          new Notice("MicroJournal: " + (r.ok ? "✓ " : "✗ ") + r.message);
        })
      );

    new Setting(containerEl)
      .setName("Reset sync state")
      .setDesc("Forget what was synced. Next sync treats every file as new (keep-both conflict still applies).")
      .addButton((b) =>
        b.setButtonText("Reset").setWarning().onClick(async () => {
          this.plugin.state = {};
          await this.plugin.saveAll();
          new Notice("MicroJournal: sync state cleared");
        })
      );
  }
}
