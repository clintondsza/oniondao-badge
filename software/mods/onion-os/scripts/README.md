# Onion OS Lua Scripts

Example Lua scripts in this directory are meant to be copied into the Onion OS
script registry or served through the script manifest.

## Image Browser

`image-browser.lua` browses every downloaded image stored in SPIFFS as
`/images_*.pbm` or `/images_*.bmp`.

Controls:

- `UP` / `LEFT`: previous image
- `DOWN` / `RIGHT`: next image
- `SELECT`: redraw current image
- `CANCEL`: return to Onion OS

To install it through a manifest, serve this script and the image assets:

```json
{
  "scripts": [
    {
      "name": "image-browser.lua",
      "url": "https://example.com/image-browser.lua"
    }
  ],
  "images": [
    {
      "name": "poster.pbm",
      "url": "https://example.com/poster.pbm"
    }
  ]
}
```
