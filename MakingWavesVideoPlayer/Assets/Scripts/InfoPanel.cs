using UnityEngine;
using UnityEngine.InputSystem;

public class InfoPanel : MonoBehaviour
{
    private bool CurrentlyVisible { get; set; }
    private GameObject infoPanel;

    void Start()
    {
        infoPanel = transform.Find("Panel").gameObject;
    }

    // Update is called once per frame
    void Update()
    {
        if (Keyboard.current != null && Keyboard.current.f1Key.wasPressedThisFrame)
        {
            CurrentlyVisible = !CurrentlyVisible;
            infoPanel.SetActive(CurrentlyVisible);
        }
    }
}
