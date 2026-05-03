// Cloudflare Worker – Orbis Store API
// KV binding required: REDEEM_STORE

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

const CODE_CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

function jsonResponse(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json', ...CORS },
  });
}

function generateCode() {
  const buf = new Uint8Array(8);
  crypto.getRandomValues(buf);
  return Array.from(buf, b => CODE_CHARS[b % CODE_CHARS.length]).join('');
}

// Parse a GitHub Releases URL and fetch metadata + first .pkg asset
async function resolveGitHubRelease(rawUrl) {
  const m = rawUrl.match(
    /https?:\/\/github\.com\/([^/]+)\/([^/]+)\/releases(?:\/tag\/([^/?#]+))?/
  );
  if (!m) return null;

  const [, owner, repo, tag] = m;
  const apiUrl = tag
    ? `https://api.github.com/repos/${owner}/${repo}/releases/tags/${tag}`
    : `https://api.github.com/repos/${owner}/${repo}/releases/latest`;

  const res = await fetch(apiUrl, {
    headers: { 'User-Agent': 'orbis-store/1.0' },
  });
  if (!res.ok) return null;

  const release = await res.json();
  const pkgAsset = (release.assets || []).find(a =>
    a.name.toLowerCase().endsWith('.pkg')
  );

  return {
    name: release.name || `${owner}/${repo}`,
    version: release.tag_name || '1.0.0',
    description: (release.body || '').slice(0, 300),
    pkg_url: pkgAsset ? pkgAsset.browser_download_url : '',
    icon_url: `https://avatars.githubusercontent.com/${owner}`,
  };
}

// Build metadata for a direct .pkg URL
function resolveDirectPkg(rawUrl) {
  try {
    const u = new URL(rawUrl);
    const segment = u.pathname.split('/').filter(Boolean).pop() || '';
    const filename = segment.replace(/\.pkg$/i, '') || 'Unknown';
    return {
      name: filename,
      version: '1.0.0',
      description: 'Direct PKG installation',
      pkg_url: rawUrl,
      icon_url: '',
    };
  } catch {
    return null;
  }
}

async function handleCreate(request, env) {
  let body;
  try {
    body = await request.json();
  } catch {
    return jsonResponse({ success: false, error: 'Invalid JSON body' }, 400);
  }

  const { url, type } = body;
  if (!url || typeof url !== 'string' || !url.trim()) {
    return jsonResponse({ success: false, error: 'url is required' }, 400);
  }

  // Resolve metadata – verify hostname to avoid path-confusion attacks
  let meta = null;
  try {
    const parsedUrl = new URL(url.trim());
    if (parsedUrl.hostname === 'github.com' &&
        parsedUrl.pathname.includes('/releases')) {
      meta = await resolveGitHubRelease(url.trim());
    }
  } catch {
    // Not a valid absolute URL or not GitHub – fall through to direct PKG handler
  }
  if (!meta) {
    meta = resolveDirectPkg(url.trim());
  }
  if (!meta) {
    return jsonResponse({ success: false, error: 'Could not resolve URL' }, 422);
  }

  if (!meta.pkg_url) {
    return jsonResponse({ success: false, error: 'No PKG file found at the provided URL' }, 422);
  }

  // Generate a unique code (retry up to 5 times on collision)
  let code = '';
  for (let i = 0; i < 5; i++) {
    const candidate = generateCode();
    if (!(await env.REDEEM_STORE.get(candidate))) {
      code = candidate;
      break;
    }
  }
  if (!code) {
    return jsonResponse({ success: false, error: 'Code generation failed, try again' }, 500);
  }

  const record = {
    ...meta,
    type: type === 'premium' ? 'premium' : 'free',
    created_at: new Date().toISOString(),
  };

  // free = 24 h TTL, premium = 30 days
  const ttl = record.type === 'premium' ? 2_592_000 : 86_400;
  await env.REDEEM_STORE.put(code, JSON.stringify(record), { expirationTtl: ttl });

  return jsonResponse({ success: true, code, ...meta });
}

async function handleRedeemByCode(code, env) {
  const normalised = (code || '').trim().toUpperCase();
  if (!normalised) {
    return jsonResponse({ valid: false, error: 'code is required' }, 400);
  }

  const raw = await env.REDEEM_STORE.get(normalised);
  if (!raw) {
    return jsonResponse({ valid: false, error: 'Invalid or expired code' }, 404);
  }

  const record = JSON.parse(raw);
  return jsonResponse({
    valid: true,
    name: record.name,
    version: record.version,
    description: record.description,
    pkg_url: record.pkg_url,
    icon_url: record.icon_url,
  });
}

export default {
  async fetch(request, env) {
    // Pre-flight
    if (request.method === 'OPTIONS') {
      return new Response(null, { headers: CORS });
    }

    const { pathname } = new URL(request.url);

    try {
      // POST /create  – generate a redeem code for a URL
      if (pathname === '/create' && request.method === 'POST') {
        return handleCreate(request, env);
      }

      // GET /api/redeem/:code  – PS4 app uses this
      const restMatch = pathname.match(/^\/api\/redeem\/([A-Za-z0-9]+)$/);
      if (restMatch && request.method === 'GET') {
        return handleRedeemByCode(restMatch[1], env);
      }

      // POST /redeem  – legacy endpoint used by the web redeem page
      if (pathname === '/redeem' && request.method === 'POST') {
        let body;
        try {
          body = await request.json();
        } catch {
          return jsonResponse({ success: false, error: 'Invalid JSON body' }, 400);
        }
        const result = await handleRedeemByCode(body.code, env);
        // Re-wrap into legacy {success, url} shape for old web page
        const data = await result.clone().json();
        if (data.valid) {
          return jsonResponse({ success: true, url: data.pkg_url, ...data });
        }
        return jsonResponse({ success: false, error: data.error });
      }

      return jsonResponse({ error: 'Not found' }, 404);
    } catch (err) {
      return jsonResponse({ error: err.message || 'Internal server error' }, 500);
    }
  },
};
