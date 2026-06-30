using UnityEngine;
using UnityEngine.InputSystem;

public class InfoPanel : MonoBehaviour
{
    private bool CurrentlyVisible { get; set; }
    private CanvasGroup infoPanelGroup;

    void Start()
    {
        infoPanelGroup = gameObject.GetComponent<CanvasGroup>();
    }

    // Update is called once per frame
    void Update()
    {
        if (Keyboard.current != null && Keyboard.current.f1Key.wasPressedThisFrame)
        {
            CurrentlyVisible = !CurrentlyVisible;
            infoPanelGroup.alpha = CurrentlyVisible ? 1 : 0;
        }
    }
}
