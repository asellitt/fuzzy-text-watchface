module.exports = [
  {
    "type": "heading",
    "defaultValue": "Fuzzy Text"
  },
  {
    "type": "text",
    "defaultValue": "Watchface settings."
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Appearance",
        "size": 4
      },
      {
        "type": "toggle",
        "messageKey": "DARK_MODE",
        "label": "Dark mode",
        "description": "White text on black background. Turn off for a light theme.",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
