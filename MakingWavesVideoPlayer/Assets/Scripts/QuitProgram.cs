using UnityEngine;

public class QuitProgram : MonoBehaviour
{
    void Update()
    {
        // Check if the ESC key is pressed
        if (Input.GetKeyDown(KeyCode.Escape))
        {
            QuitGame();
        }
    }

    // Function to quit the game
    private void QuitGame()
    {
        // If running in the Unity Editor
        #if UNITY_EDITOR
            UnityEditor.EditorApplication.isPlaying = false;  // Stops play mode in the editor
        #else
            Application.Quit();  // Quits the game in a built version
        #endif
    }
}

