using UnityEngine;
using UnityEngine.Video;

public class  ResetVideo : MonoBehaviour
{
    private Renderer objectRenderer;
    private VideoPlayer videoPlayer;

    void Start()
    {
        // Get references to the Renderer and VideoPlayer components
        objectRenderer = GetComponent<Renderer>();
        videoPlayer = GetComponent<VideoPlayer>();
    }

    void Update()
    {
        // Check if object is visible on the screen
        if (objectRenderer.isVisible || videoPlayer.frame == 5)
        {
            return;
        }
        
        // Set the video to the first frame if it's off-screen
        videoPlayer.frame = 0;
        videoPlayer.Pause();
    }
}
