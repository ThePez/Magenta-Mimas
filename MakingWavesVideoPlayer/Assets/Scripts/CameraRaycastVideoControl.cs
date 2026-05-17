using UnityEngine;
using UnityEngine.Video;

public class CameraRaycastVideoControl : MonoBehaviour
{
    [Header("Configuration")] 
    public float raycastDistance = 30f;

    public float zoomSpeed = 1f;
    public float targetFOV = 15f;
    public float normalFOV = 60f;
    public float rotationSpeed = 5f;

    [Header("Centering thresholds (fractions of screen width)")]
    public float centerThresholdEnter = 10f;

    // -------------------------------------------------
    // Private state
    // -------------------------------------------------
    private float originalPitch; // Camera pitch at start
    
    private PlayerRotateWithInertia playerRotation;
    private VideoPlayer lastVideoPlayer;
    private VideoPlayer[] videoPlayers;
    private Camera mainCamera;

    void Start()
    {
        mainCamera = gameObject.GetComponent<Camera>();
        playerRotation = GameObject.FindGameObjectWithTag("Player").GetComponent<PlayerRotateWithInertia>();
        
        originalPitch = mainCamera.transform.eulerAngles.x;
        videoPlayers = FindObjectsByType<VideoPlayer>();
    }

    void Update()
    {
        Transform parent = transform.parent;

        foreach (VideoPlayer vp in videoPlayers)
        {
            Vector3 rayOrigin = parent.position;
            rayOrigin.y = vp.transform.position.y;
            Ray ray = new(rayOrigin, parent.forward);
            
            Debug.DrawRay(rayOrigin, rayOrigin + parent.forward * raycastDistance);

            if (!Physics.Raycast(ray, out RaycastHit hitInfo, raycastDistance))
            {
                continue;
            }

            VideoPlayer videoPlayer = hitInfo.collider.GetComponent<VideoPlayer>();
            
            if (lastVideoPlayer != videoPlayer && playerRotation.Velocity == 0)
            {
                if (!videoPlayer.isPlaying)
                {
                    videoPlayer.Play();
                }

                lastVideoPlayer = videoPlayer;
            }
                
            // **Zoom and rotate regardless of pause state** – the video remains playing
            if (playerRotation.Velocity == 0)
            {
                ZoomCamera(targetFOV);

                Quaternion current = mainCamera.transform.rotation;
                current.SetLookRotation(hitInfo.transform.position - mainCamera.transform.position);

                mainCamera.transform.rotation = Quaternion.Slerp(mainCamera.transform.rotation.normalized,
                    current.normalized, rotationSpeed * Time.deltaTime);
            }
            else
            {
                ZoomCamera(normalFOV);
                RotateCamera(originalPitch);
            }

            return;
        }
        
        // Ray hit nothing – pause everything
        ZoomCamera(normalFOV);
        RotateCamera(originalPitch);
            
        foreach (VideoPlayer vp in videoPlayers)
        {
            if (vp.isPlaying)
            {
                vp.Pause();
            }
        }
        
        lastVideoPlayer = null;
    }

    // -------------------------------------------------
    // Zoom and rotation helpers (clamped)
    // -------------------------------------------------
    private void ZoomCamera(float newZoom)
    {
        mainCamera.fieldOfView = Mathf.Lerp(mainCamera.fieldOfView, newZoom, zoomSpeed * Time.deltaTime);
    }

    private void RotateCamera(float newPitch)
    {
        Quaternion targetRot = Quaternion.Euler(newPitch, mainCamera.transform.eulerAngles.y,
            mainCamera.transform.eulerAngles.z).normalized;
        mainCamera.transform.rotation = Quaternion.RotateTowards(mainCamera.transform.rotation.normalized, targetRot, 
            rotationSpeed * Time.deltaTime);
    }
}