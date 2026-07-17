// PUT THE FOLDER PATH OF YOUR LIKING
// FOLDER SHOULD EXIST SO CREATE ONE BEFORE SYNCING
const _FOLDER_PATH = "/MicroJournal";

// Shared secret. Set this to a long random string, then:
//   - device  config.json : sync.url = ".../exec?token=YOUR_SECRET"
//   - Obsidian plugin     : paste the same token in settings
// Leave "" to keep the endpoint open (NOT recommended once reads are enabled).
const _TOKEN = "";

// ---- WRITE: device + Obsidian push a file (base64 body, ?name=) --------------
function doPost(e) {
  try {
    if (!checkToken(e)) return jsonOut({ status: "ERROR", message: "unauthorized" });

    var fileContent = e.postData.contents;

    // The body is base64 of the file's raw UTF-8 bytes. Decode straight to a
    // UTF-8 string. (The old ISO-8859-1 round-trip double-encoded multibyte
    // text, which garbled Arabic bodies while leaving ASCII intact.)
    var bytes = Utilities.base64Decode(fileContent);
    fileContent = Utilities.newBlob(bytes).getDataAsString("UTF-8");

    // Filename: if ?name= is given (URL-encoded, decoded here automatically),
    // save under that name and OVERWRITE any existing copy so re-syncing updates
    // one file instead of piling up duplicates. With no name, fall back to the
    // old timestamped name.
    var requestedName = e.parameter && e.parameter.name ? e.parameter.name : "";
    var fileName = requestedName ? requestedName : getFormattedDate() + "_uJournal.txt";

    var folder = getFolder(_FOLDER_PATH);
    var file = null;

    // Prefer updating an existing file by id (survives renames on either side →
    // never creates a duplicate). A stale/trashed id falls through to by-name.
    var id = e.parameter && e.parameter.id ? e.parameter.id : "";
    if (id) {
      try {
        var byId = DriveApp.getFileById(id);
        if (byId && !byId.isTrashed()) {
          byId.setContent(fileContent);
          if (requestedName && byId.getName() !== requestedName)
            byId.setName(requestedName);
          file = byId;
        }
      } catch (err) {
        file = null;
      }
    }

    if (!file) {
      var existing = folder.getFilesByName(fileName);
      if (requestedName && existing.hasNext()) {
        file = existing.next();
        file.setContent(fileContent);
      } else {
        file = folder.createFile(fileName, fileContent, "text/plain; charset=utf-8");
      }
    }

    return jsonOut(fileInfo(file));
  } catch (error) {
    sendErrorEmail(error, e);
    return jsonOut({ status: "ERROR", message: "An error occurred" });
  }
}

// ---- READ / RENAME / TRASH: the Obsidian plugin -----------------------------
function doGet(e) {
  try {
    if (!checkToken(e)) return jsonOut({ status: "ERROR", message: "unauthorized" });

    var action = (e.parameter && e.parameter.action) || "";
    var folder = getFolder(_FOLDER_PATH);
    if (!folder) return jsonOut({ status: "ERROR", message: "folder not found" });

    if (action === "list") {
      var files = [];
      var it = folder.getFiles();
      while (it.hasNext()) {
        var f = it.next();
        // skip Google-native items (the Apps Script project itself, Docs, …) —
        // only real uploaded text files round-trip to the vault.
        if (f.getMimeType().indexOf("application/vnd.google-apps") === 0) continue;
        files.push({
          id: f.getId(),
          name: f.getName(),
          modified: f.getLastUpdated().getTime(),
          size: f.getSize(),
        });
      }
      return jsonOut({ status: "OK", files: files });
    }

    if (action === "get") {
      var file = resolveFile(e, folder);
      if (!file) return jsonOut({ status: "ERROR", message: "not found" });
      var info = fileInfo(file);
      // base64 of raw bytes so Arabic round-trips intact through JSON.
      info.content = Utilities.base64Encode(file.getBlob().getBytes());
      return jsonOut(info);
    }

    if (action === "rename") {
      var file = resolveFile(e, folder);
      var name = e.parameter && e.parameter.name ? e.parameter.name : "";
      if (!file || !name) return jsonOut({ status: "ERROR", message: "not found" });
      file.setName(name);
      return jsonOut(fileInfo(file));
    }

    if (action === "trash") {
      var file = resolveFile(e, folder);
      if (!file) return jsonOut({ status: "ERROR", message: "not found" });
      file.setTrashed(true); // recoverable from Drive trash, not a hard delete
      return jsonOut({ status: "OK", id: file.getId() });
    }

    return jsonOut({ status: "ERROR", message: "unknown action" });
  } catch (error) {
    sendErrorEmail(error, e);
    return jsonOut({ status: "ERROR", message: "An error occurred" });
  }
}

// Resolve a target file by id (preferred, survives renames) or by name.
function resolveFile(e, folder) {
  var id = e.parameter && e.parameter.id ? e.parameter.id : "";
  if (id) {
    try {
      return DriveApp.getFileById(id);
    } catch (err) {
      return null;
    }
  }
  var name = e.parameter && e.parameter.name ? e.parameter.name : "";
  if (name) {
    var ex = folder.getFilesByName(name);
    if (ex.hasNext()) return ex.next();
  }
  return null;
}

function fileInfo(file) {
  return {
    status: "OK",
    id: file.getId(),
    name: file.getName(),
    modified: file.getLastUpdated().getTime(),
  };
}

function checkToken(e) {
  if (!_TOKEN) return true; // open endpoint
  return e && e.parameter && e.parameter.token === _TOKEN;
}

function jsonOut(obj) {
  var out = ContentService.createTextOutput(JSON.stringify(obj));
  out.setMimeType(ContentService.MimeType.JSON);
  return out;
}

function sendErrorEmail(error, e) {
  var recipient = Session.getEffectiveUser().getEmail();
  var subject = "Error in Micro Journal Sync Process";
  var body = error.stack + "\n\n" + JSON.stringify(e);
  MailApp.sendEmail(recipient, subject, body);
}

function getFormattedDate() {
  var now = new Date();
  var year = now.getFullYear();
  var month = padZero(now.getMonth() + 1);
  var day = padZero(now.getDate());
  var hours = padZero(now.getHours());
  var minutes = padZero(now.getMinutes());
  return year + "." + month + "." + day + "-" + hours + "." + minutes;
}

function padZero(num) {
  return (num < 10 ? "0" : "") + num;
}

function getFolder(folderPath) {
  var folders = folderPath.split("/");
  var folder = DriveApp.getRootFolder();
  for (var i = 0; i < folders.length; i++) {
    if (folders[i] !== "") {
      var subFolders = folder.getFoldersByName(folders[i]);
      if (subFolders.hasNext()) {
        folder = subFolders.next();
      } else {
        return null;
      }
    }
  }
  return folder;
}
