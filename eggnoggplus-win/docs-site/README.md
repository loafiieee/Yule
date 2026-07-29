# Eggnogg+ Developer Docs site

This directory is a static site with no build step or dependencies.

- Publish or serve the contents of `docs-site/` as the web root.
- Use `index.html` as the landing file.
- Keep the directory structure unchanged; pages, CSS, JavaScript, individual API
  anchors, and search entries use relative links.

The reference is data-driven: `api-data.js` contains the per-function contracts and
`app.js` renders the common navigation, search, per-page contents, API filters, and
function entries. Guides and conceptual references remain ordinary semantic HTML.

For a local HTTP preview from the repository root:

```powershell
python -m http.server 8000 --directory docs-site
```

Then open `http://localhost:8000/`.

Run the dependency-free structure, source-API coverage, and link audit:

```powershell
node docs-site/check-docs.js
node --check docs-site/app.js
node --check docs-site/api-data.js
```
