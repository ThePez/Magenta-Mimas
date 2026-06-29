using TMPro;
using UnityEngine;
using UnityEngine.InputSystem;

public class BatteryViewer : MonoBehaviour
{
    private record BatteryKeys(Key Key, Color Color, string Text)
    {
        public Key Key { get; } = Key;
        public Color Color { get; } = Color;
        public string Text { get; } = Text;
    }
    
    private static readonly BatteryKeys[] Keys =
    {
        new(Key.Digit1, new Color(245f/255, 66f/255,  66f/255), "10%"),
        new(Key.Digit2, new Color(245f/255, 93f/255,  66f/255), "20%"),
        new(Key.Digit3, new Color(245f/255, 126f/255, 66f/255), "30%"),
        new(Key.Digit4, new Color(245f/255, 161f/255, 66f/255), "40%"),
        new(Key.Digit5, new Color(245f/255, 200f/255, 66f/255), "50%"),
        new(Key.Digit6, new Color(245f/255, 221f/255, 66f/255), "60%"),
        new(Key.Digit7, new Color(239f/255, 245f/255, 66f/255), "70%"),
        new(Key.Digit8, new Color(212f/255, 245f/255, 66f/255), "80%"),
        new(Key.Digit9, new Color(180f/255, 245f/255, 66f/255), "90%"),
        new(Key.Digit0, new Color(130f/255, 245f/255, 66f/255), "100%"),
    };

    private TMP_Text batteryText;
    
    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        batteryText = gameObject.GetComponent<TMP_Text>();
    }

    // Update is called once per frame
    void Update()
    {
        foreach (BatteryKeys bKey in Keys)
        {
            if (Keyboard.current[bKey.Key].wasPressedThisFrame)
            {
                batteryText.text = bKey.Text;
                batteryText.color = bKey.Color;
                break;
            }
        }
    }
}
